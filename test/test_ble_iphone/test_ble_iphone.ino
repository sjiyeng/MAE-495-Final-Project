/*
  =============================================================================
  ASME e-HPVC Telemetry System - iPhone 17 BLE IMU & GPS Passthrough Test
  Target Hardware: Seeed Studio XIAO ESP32S3 (Transmitter Node)
  iOS App Required: "Bluefruit Connect" (by Adafruit Industries - Free on App Store)
  Libraries Required: Built-in ESP32 BLE Arduino library (no external install needed)
  =============================================================================
  
  HOW IT WORKS:
  1. Advertises a Nordic UART Service (NUS) BLE peripheral named "ASME-Trike-TX".
  2. Connects to the "Bluefruit Connect" app on iPhone 17 -> Controller module.
  3. Parses Adafruit Bluefruit binary sensor packets over BLE UART:
     - '!Q' (19 bytes): Fused 9-DoF Quaternion (x, y, z, w) -> Heading, Roll, Pitch
     - '!A' (15 bytes): 3-Axis Accelerometer (ax, ay, az in G)
     - '!M' (15 bytes): 3-Axis Magnetometer (mx, my, mz in uT) -> Mag Compass Heading
     - '!G' (15 bytes): 3-Axis Gyroscope (gx, gy, gz in rad/s)
     - '!L' (15 bytes): GPS Location (Latitude, Longitude, Altitude)
  4. Prints IMU Heading at 20 Hz (every 50 ms) to the Serial Monitor at 115200 baud.
  =============================================================================
*/

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>

// -----------------------------------------------------------------------------
// Nordic UART Service (NUS) UUIDs (Required by Adafruit Bluefruit Connect app)
// -----------------------------------------------------------------------------
#define BLE_DEVICE_NAME           "ASME-Trike-TX"
#define SERVICE_UUID              "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX    "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // iPhone -> ESP32S3
#define CHARACTERISTIC_UUID_TX    "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // ESP32S3 -> iPhone

BLEServer* pServer = nullptr;
BLECharacteristic* pTxCharacteristic = nullptr;
volatile bool deviceConnected = false;
bool oldDeviceConnected = false;

// -----------------------------------------------------------------------------
// Telemetry State Variables (Updated asynchronously as BLE packets arrive)
// -----------------------------------------------------------------------------
struct PhoneTelemetry {
  // Quaternion ('!Q') & Reference Quaternion for Arbitrary Mounting Orientation
  float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
  float refQx = 0.0f, refQy = 0.0f, refQz = 0.0f, refQw = 1.0f;
  float refCompassOffsetDeg = 0.0f;
  bool  hasRefQuat     = false;

  float quatHeadingDeg = 0.0f; // 0 - 360 deg (Orientation-Independent World Yaw)
  float rollDeg        = 0.0f; // Relative to initial mounting pose
  float pitchDeg       = 0.0f; // Relative to initial mounting pose
  bool  hasQuat        = false;
  uint32_t quatCount   = 0;

  // Magnetometer ('!M')
  float magX = 0.0f, magY = 0.0f, magZ = 0.0f;
  float magHeadingDeg  = 0.0f; // 0 - 360 deg (Magnetic Compass)
  bool  hasMag         = false;
  uint32_t magCount    = 0;

  // Accelerometer ('!A') - Bluefruit iOS sends in G's (1 G ≈ 9.80665 m/s^2)
  float accelX_G = 0.0f, accelY_G = 0.0f, accelZ_G = 0.0f;
  bool  hasAccel       = false;
  uint32_t accelCount  = 0;

  // Gyroscope ('!G')
  float gyroX = 0.0f, gyroY = 0.0f, gyroZ = 0.0f;
  bool  hasGyro        = false;
  uint32_t gyroCount   = 0;

  // GPS Location ('!L')
  float latitude  = 0.0f;
  float longitude = 0.0f;
  float altitude  = 0.0f;
  bool  hasGPS    = false;
  uint32_t gpsCount = 0;
} telemetry;

portMUX_TYPE telemetryMux = portMUX_INITIALIZER_UNLOCKED;

// Ring buffer for reassembling BLE byte streams into complete '!X' packets
#define RX_BUF_SIZE 256
uint8_t rxBuf[RX_BUF_SIZE];
size_t  rxLen = 0;

// -----------------------------------------------------------------------------
// Helper: Verify Adafruit Bluefruit Packet Checksum (~sum of bytes[0..len-2])
// -----------------------------------------------------------------------------
bool verifyChecksum(const uint8_t* pkt, size_t totalLen) {
  uint8_t sum = 0;
  for (size_t i = 0; i < totalLen - 1; i++) {
    sum += pkt[i];
  }
  uint8_t expectedCrc = ~sum;
  return (pkt[totalLen - 1] == expectedCrc);
}

// -----------------------------------------------------------------------------
// Helper: Read IEEE-754 Little-Endian Float from Byte Buffer
// -----------------------------------------------------------------------------
float readFloatLE(const uint8_t* ptr) {
  float val;
  memcpy(&val, ptr, sizeof(float));
  return val;
}

// -----------------------------------------------------------------------------
// Helper: Compute Initial Compass Heading from Any Phone Orientation
// Projects both Phone +Y (top edge) and Phone -Z (rear camera) onto the
// World Horizontal (XY) plane and picks whichever axis is more horizontal.
// -----------------------------------------------------------------------------
float computeInitialCompassHeading(float x, float y, float z, float w) {
  // Phone +Y axis (top of phone) in World XY plane:
  float yWorldX = 2.0f * (x * y - w * z);
  float yWorldY = 1.0f - 2.0f * (x * x + z * z);
  float yHorizMagSq = yWorldX * yWorldX + yWorldY * yWorldY;

  // Phone -Z axis (out the back camera) in World XY plane:
  float negZWorldX = -2.0f * (x * z + w * y);
  float negZWorldY = -2.0f * (y * z - w * x);
  float zHorizMagSq = negZWorldX * negZWorldX + negZWorldY * negZWorldY;

  // Choose whichever axis is closer to horizontal (avoids vertical gimbal lock)
  float fwdX = (yHorizMagSq >= zHorizMagSq) ? yWorldX : negZWorldX;
  float fwdY = (yHorizMagSq >= zHorizMagSq) ? yWorldY : negZWorldY;

  float yawDeg = atan2f(fwdX, fwdY) * (180.0f / PI);
  if (yawDeg < 0.0f) yawDeg += 360.0f;
  return yawDeg;
}

// -----------------------------------------------------------------------------
// Helper: Parse a Complete Validated Bluefruit Controller Packet
// -----------------------------------------------------------------------------
void parseBluefruitPacket(const uint8_t* pkt, size_t len) {
  char type = (char)pkt[1];

  if (type == 'Q' && len == 19) {
    // Quaternion: '!', 'Q', qx, qy, qz, qw, crc
    float x = readFloatLE(&pkt[2]);
    float y = readFloatLE(&pkt[6]);
    float z = readFloatLE(&pkt[10]);
    float w = readFloatLE(&pkt[14]);

    // Normalize quaternion to prevent numerical drift
    float norm = sqrtf(x * x + y * y + z * z + w * w);
    if (norm > 0.0001f) {
      x /= norm; y /= norm; z /= norm; w /= norm;
    }

    portENTER_CRITICAL(&telemetryMux);
    // Capture reference mounting orientation on the first valid packet
    if (!telemetry.hasRefQuat) {
      telemetry.refQx = x;
      telemetry.refQy = y;
      telemetry.refQz = z;
      telemetry.refQw = w;
      telemetry.refCompassOffsetDeg = computeInitialCompassHeading(x, y, z, w);
      telemetry.hasRefQuat = true;
    }

    // Compute World-Frame rotation relative to the initial mounting pose:
    // dQ_world = Q_current * conjugate(Q_ref)
    // Because iOS CoreMotion's World +Z axis is ALWAYS aligned with gravity (straight up),
    // dQ_world measures pure horizontal rotation around World Z regardless of how the
    // iPhone is mounted (flat, vertical, sideways, or tilted at any angle).
    float rQx = -telemetry.refQx;
    float rQy = -telemetry.refQy;
    float rQz = -telemetry.refQz;
    float rQw =  telemetry.refQw;

    float dQw = w * rQw - x * rQx - y * rQy - z * rQz;
    float dQx = w * rQx + x * rQw + y * rQz - z * rQy;
    float dQy = w * rQy - x * rQz + y * rQw + z * rQx;
    float dQz = w * rQz + x * rQy - y * rQx + z * rQw;

    // Extract relative Roll, Pitch, and World-Z Yaw from dQ_world
    float sinr_cosp = 2.0f * (dQw * dQx + dQy * dQz);
    float cosr_cosp = 1.0f - 2.0f * (dQx * dQx + dQy * dQy);
    float roll = atan2f(sinr_cosp, cosr_cosp) * (180.0f / PI);

    float sinp = 2.0f * (dQw * dQy - dQz * dQx);
    float pitch = (fabsf(sinp) >= 1.0f) ? copysignf(90.0f, sinp) : (asinf(sinp) * (180.0f / PI));

    // Yaw around the true vertical (gravity) World Z-axis:
    float siny_cosp = 2.0f * (dQw * dQz + dQx * dQy);
    float cosy_cosp = 1.0f - 2.0f * (dQy * dQy + dQz * dQz);
    float deltaYawDeg = atan2f(siny_cosp, cosy_cosp) * (180.0f / PI);

    // Combine with initial compass offset and wrap to [0, 360) deg CW
    float heading = fmodf(telemetry.refCompassOffsetDeg - deltaYawDeg + 360.0f, 360.0f);
    if (heading < 0.0f) heading += 360.0f;

    telemetry.qx = x;
    telemetry.qy = y;
    telemetry.qz = z;
    telemetry.qw = w;
    telemetry.rollDeg = roll;
    telemetry.pitchDeg = pitch;
    telemetry.quatHeadingDeg = heading;
    telemetry.hasQuat = true;
    telemetry.quatCount++;
    portEXIT_CRITICAL(&telemetryMux);

  } else if (type == 'A' && len == 15) {
    // Accelerometer: '!', 'A', ax, ay, az, crc (in G's on iOS)
    float ax = readFloatLE(&pkt[2]);
    float ay = readFloatLE(&pkt[6]);
    float az = readFloatLE(&pkt[10]);

    portENTER_CRITICAL(&telemetryMux);
    telemetry.accelX_G = ax;
    telemetry.accelY_G = ay;
    telemetry.accelZ_G = az;
    telemetry.hasAccel = true;
    telemetry.accelCount++;
    portEXIT_CRITICAL(&telemetryMux);

  } else if (type == 'M' && len == 15) {
    // Magnetometer: '!', 'M', mx, my, mz, crc (in uT)
    float mx = readFloatLE(&pkt[2]);
    float my = readFloatLE(&pkt[6]);
    float mz = readFloatLE(&pkt[10]);

    // Tilt-uncompensated magnetic heading in [0, 360) deg
    float magHeading = atan2f(my, mx) * (180.0f / PI);
    if (magHeading < 0.0f) magHeading += 360.0f;

    portENTER_CRITICAL(&telemetryMux);
    telemetry.magX = mx;
    telemetry.magY = my;
    telemetry.magZ = mz;
    telemetry.magHeadingDeg = magHeading;
    telemetry.hasMag = true;
    telemetry.magCount++;
    portEXIT_CRITICAL(&telemetryMux);

  } else if (type == 'G' && len == 15) {
    // Gyroscope: '!', 'G', gx, gy, gz, crc
    float gx = readFloatLE(&pkt[2]);
    float gy = readFloatLE(&pkt[6]);
    float gz = readFloatLE(&pkt[10]);

    portENTER_CRITICAL(&telemetryMux);
    telemetry.gyroX = gx;
    telemetry.gyroY = gy;
    telemetry.gyroZ = gz;
    telemetry.hasGyro = true;
    telemetry.gyroCount++;
    portEXIT_CRITICAL(&telemetryMux);

  } else if (type == 'L' && len == 15) {
    // Location (GPS): '!', 'L', lat, lon, alt, crc
    float lat = readFloatLE(&pkt[2]);
    float lon = readFloatLE(&pkt[6]);
    float alt = readFloatLE(&pkt[10]);

    portENTER_CRITICAL(&telemetryMux);
    telemetry.latitude  = lat;
    telemetry.longitude = lon;
    telemetry.altitude  = alt;
    telemetry.hasGPS    = true;
    telemetry.gpsCount++;
    portEXIT_CRITICAL(&telemetryMux);
  }
}

// -----------------------------------------------------------------------------
// Stream Parser: Handles single or concatenated packets arriving over BLE RX
// -----------------------------------------------------------------------------
void processIncomingBytes(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (rxLen < RX_BUF_SIZE) {
      rxBuf[rxLen++] = data[i];
    } else {
      // Overflow protection: reset buffer
      rxLen = 0;
      rxBuf[rxLen++] = data[i];
    }
  }

  // Scan buffer for valid '!X' packets
  size_t idx = 0;
  while (idx < rxLen) {
    // Find start byte '!'
    if (rxBuf[idx] != '!') {
      idx++;
      continue;
    }

    // Need at least 2 bytes to know the packet type
    if (idx + 1 >= rxLen) {
      break;
    }

    char type = (char)rxBuf[idx + 1];
    size_t expectedLen = 0;

    switch (type) {
      case 'Q': expectedLen = 19; break; // '!' + 'Q' + 4 floats (16B) + CRC (1B)
      case 'A':                          // '!' + 'A' + 3 floats (12B) + CRC (1B)
      case 'G':                          // '!' + 'G' + 3 floats (12B) + CRC (1B)
      case 'M':                          // '!' + 'M' + 3 floats (12B) + CRC (1B)
      case 'L': expectedLen = 15; break; // '!' + 'L' + 3 floats (12B) + CRC (1B)
      default:
        // Unknown packet type; advance past this '!'
        idx++;
        continue;
    }

    // Wait for more bytes if the full packet hasn't arrived yet
    if (idx + expectedLen > rxLen) {
      break;
    }

    // Verify checksum and parse
    if (verifyChecksum(&rxBuf[idx], expectedLen)) {
      parseBluefruitPacket(&rxBuf[idx], expectedLen);
      idx += expectedLen;
    } else {
      // False '!' byte inside float payload; step forward 1 byte
      idx++;
    }
  }

  // Shift any remaining partial packet bytes to the front of rxBuf
  if (idx > 0) {
    if (idx < rxLen) {
      memmove(rxBuf, &rxBuf[idx], rxLen - idx);
      rxLen -= idx;
    } else {
      rxLen = 0;
    }
  }
}

// -----------------------------------------------------------------------------
// BLE Server & Characteristic Callbacks
// -----------------------------------------------------------------------------
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServerInstance) override {
    deviceConnected = true;
  }

  void onDisconnect(BLEServer* pServerInstance) override {
    deviceConnected = false;
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    uint8_t* data = pCharacteristic->getData();
    size_t len    = pCharacteristic->getLength();
    if (data != nullptr && len > 0) {
      processIncomingBytes(data, len);
    }
  }
};

// -----------------------------------------------------------------------------
// Timing for 20 Hz (Every 50 ms) Serial Output
// -----------------------------------------------------------------------------
unsigned long lastPrintTime = 0;
const unsigned long PRINT_INTERVAL_MS = 50; // 50 ms = 20 Hz

void setup() {
  Serial.begin(115200);
  delay(1500); // Allow USB CDC Serial Monitor / Plotter to connect

  // 1. Initialize BLE Device
  BLEDevice::init(BLE_DEVICE_NAME);

  // 2. Create BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  // 3. Create Nordic UART Service (NUS)
  BLEService* pService = pServer->createService(SERVICE_UUID);

  // 4. Create TX Characteristic (ESP32S3 -> iPhone notifications)
  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());

  // 5. Create RX Characteristic (iPhone -> ESP32S3 writes)
  BLECharacteristic* pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pRxCharacteristic->setCallbacks(new RxCallbacks());

  // 6. Start Service and Advertising (with Service UUID in advertisement packet so Bluefruit sees it)
  pService->start();
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); // Helps with iPhone BLE connection parameters
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
}

void loop() {
  unsigned long now = millis();

  // Optional: Send 'z' or 'c' in Serial Plotter / Monitor to re-calibrate mounting orientation
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'z' || cmd == 'Z') {
      portENTER_CRITICAL(&telemetryMux);
      telemetry.refQx = telemetry.qx;
      telemetry.refQy = telemetry.qy;
      telemetry.refQz = telemetry.qz;
      telemetry.refQw = telemetry.qw;
      telemetry.refCompassOffsetDeg = 0.0f; // Zeroes heading to 0.0 deg
      telemetry.hasRefQuat = true;
      portEXIT_CRITICAL(&telemetryMux);
    } else if (cmd == 'c' || cmd == 'C') {
      portENTER_CRITICAL(&telemetryMux);
      telemetry.hasRefQuat = false; // Re-locks to absolute compass heading on next packet
      portEXIT_CRITICAL(&telemetryMux);
    }
  }

  // Handle BLE connection state transitions
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
    portENTER_CRITICAL(&telemetryMux);
    telemetry.hasRefQuat = false; // Auto-capture mounting orientation on first packet
    portEXIT_CRITICAL(&telemetryMux);
  }
  if (!deviceConnected && oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
    delay(300);
    pServer->startAdvertising();
  }

  // Output at 20 Hz (every 50 ms) formatted for Arduino Serial Plotter & Web Compass Plotter
  if (now - lastPrintTime >= PRINT_INTERVAL_MS) {
    lastPrintTime = now;

    if (!deviceConnected) {
      return;
    }

    // Copy shared state safely
    PhoneTelemetry snap;
    portENTER_CRITICAL(&telemetryMux);
    snap = telemetry;
    portEXIT_CRITICAL(&telemetryMux);

    float heading = -1.0f;
    if (snap.hasQuat) {
      heading = snap.quatHeadingDeg;
    } else if (snap.hasMag) {
      heading = snap.magHeadingDeg;
    }

    if (heading >= 0.0f) {
      // Compute unit-circle X (East/Sin) and Y (North/Cos) components + 0..360 bounds for Serial Plotter
      float rad = heading * (PI / 180.0f);
      float arrowX = sinf(rad);
      float arrowY = cosf(rad);
      Serial.printf("Min:0.0,Heading:%.1f,Max:360.0,SinX:%.2f,CosY:%.2f\n",
                    heading, arrowX, arrowY);
    }
  }
}
