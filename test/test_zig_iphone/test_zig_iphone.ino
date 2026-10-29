/*
  =============================================================================
  ASME e-HPVC Telemetry System - iPhone 17 "ZIG SIM" 60 Hz Wi-Fi UDP Test
  Target Hardware: Seeed Studio XIAO ESP32S3 (Transmitter Node)
  iOS App Required: "ZIG SIM" (by 1-10, Inc. - Free on Apple App Store)
  Libraries Required: Built-in ESP32 WiFi & WiFiUdp libraries (no install needed)
  =============================================================================
  
  HOW IT WORKS:
  1. Starts a standalone Wi-Fi Access Point named "ASME-Trike-TX" (IP: 192.168.4.1).
  2. Listens on UDP Port 50000 (ZIG SIM's default port) at up to 60 Hz.
  3. Parses ZIG SIM Quaternion data (supports both JSON and OSC formats automatically).
  4. Locks the phone's 3D mounting orientation on the first packet (resettable via
     'z' or 'c' in Serial) and streams Heading to Serial Plotter / Compass Plotter.
  =============================================================================
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <math.h>

// -----------------------------------------------------------------------------
// Wi-Fi Access Point & UDP Configuration (Matches ZIG SIM Defaults)
// -----------------------------------------------------------------------------
const char* AP_SSID  = "ASME-Trike-TX"; // Open Wi-Fi network (no password needed)
const uint16_t UDP_PORT = 50000;        // Default port in ZIG SIM app

WiFiUDP udp;
char udpBuffer[2048];

// -----------------------------------------------------------------------------
// Telemetry State Variables
// -----------------------------------------------------------------------------
struct PhoneTelemetry {
  // Quaternion & Reference Quaternion for Arbitrary Mounting Orientation
  float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
  float refQx = 0.0f, refQy = 0.0f, refQz = 0.0f, refQw = 1.0f;
  float refCompassOffsetDeg = 0.0f;
  bool  hasRefQuat     = false;

  float quatHeadingDeg = 0.0f; // 0 - 360 deg (Orientation-Independent World Yaw)
  float rollDeg        = 0.0f; // Relative to initial mounting pose
  float pitchDeg       = 0.0f; // Relative to initial mounting pose
  bool  hasQuat        = false;
  uint32_t quatCount   = 0;
} telemetry;

// -----------------------------------------------------------------------------
// Timing for Serial Plotter Output (60 Hz = every 16 ms to match 60 Hz ZIG SIM)
// -----------------------------------------------------------------------------
unsigned long lastPrintTime = 0;
const unsigned long PRINT_INTERVAL_MS = 16; // ~60 Hz Serial Plotter refresh

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
// Helper: Update Heading from Normalized Quaternion (x, y, z, w)
// Uses first-packet reference pose dQ_world = Q_current * conjugate(Q_ref)
// -----------------------------------------------------------------------------
void updateQuaternionHeading(float x, float y, float z, float w) {
  float norm = sqrtf(x * x + y * y + z * z + w * w);
  if (norm < 0.0001f) return;
  x /= norm; y /= norm; z /= norm; w /= norm;

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
}

// -----------------------------------------------------------------------------
// Helper: Extract a Float Key (e.g. "\"x\":") Inside a JSON Substring
// -----------------------------------------------------------------------------
bool extractJsonFloat(const char* sectionStart, const char* sectionEnd, const char* key, float& outVal) {
  const char* p = strstr(sectionStart, key);
  if (!p || p >= sectionEnd) return false;
  p += strlen(key);
  while (p < sectionEnd && (*p == ' ' || *p == '\t' || *p == ':')) p++;
  if (p >= sectionEnd) return false;
  outVal = strtof(p, nullptr);
  return true;
}

// -----------------------------------------------------------------------------
// Helper: Read Big-Endian IEEE-754 Float (Used if ZIG SIM is set to OSC mode)
// -----------------------------------------------------------------------------
float readFloatBE(const uint8_t* ptr) {
  uint32_t raw = ((uint32_t)ptr[0] << 24) |
                 ((uint32_t)ptr[1] << 16) |
                 ((uint32_t)ptr[2] << 8)  |
                 ((uint32_t)ptr[3]);
  float val;
  memcpy(&val, &raw, sizeof(float));
  return val;
}

// -----------------------------------------------------------------------------
// Parse Incoming ZIG SIM UDP Packet (Supports BOTH JSON and OSC Formats!)
// -----------------------------------------------------------------------------
void parseZigSimPacket(const char* buf, int len) {
  // 1. Check for JSON format: {"sensordata":{"quaternion":{"x":...,"y":...,"z":...,"w":...}}}
  const char* quatSection = strstr(buf, "\"quaternion\"");
  if (quatSection) {
    const char* closeBrace = strchr(quatSection, '}');
    if (closeBrace) {
      float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
      if (extractJsonFloat(quatSection, closeBrace, "\"x\"", x) &&
          extractJsonFloat(quatSection, closeBrace, "\"y\"", y) &&
          extractJsonFloat(quatSection, closeBrace, "\"z\"", z) &&
          extractJsonFloat(quatSection, closeBrace, "\"w\"", w)) {
        updateQuaternionHeading(x, y, z, w);
        return;
      }
    }
  }

  // 2. Fallback: Check for OSC binary format ("/ZIGSIM/.../quaternion" followed by ",ffff")
  for (int i = 0; i < len - 28; i++) {
    if (memcmp(&buf[i], "quaternion", 10) == 0) {
      // Search forward for OSC type tag ",ffff"
      for (int j = i + 10; j < i + 24 && j + 24 <= len; j++) {
        if (memcmp(&buf[j], ",ffff", 5) == 0) {
          // In OSC, ",ffff\0\0\0" is padded to 8 bytes before the 4 big-endian floats
          const uint8_t* fPtr = (const uint8_t*)&buf[j + 8];
          float x = readFloatBE(fPtr + 0);
          float y = readFloatBE(fPtr + 4);
          float z = readFloatBE(fPtr + 8);
          float w = readFloatBE(fPtr + 12);
          updateQuaternionHeading(x, y, z, w);
          return;
        }
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  // 1. Start Wi-Fi SoftAP (IP defaults to 192.168.4.1)
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  delay(200);

  // 2. Start listening for ZIG SIM UDP packets on port 50000
  udp.begin(UDP_PORT);
}

void loop() {
  unsigned long now = millis();

  // Optional: Send 'z' or 'c' in Serial Plotter / Monitor / Compass Plotter
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'z' || cmd == 'Z') {
      telemetry.refQx = telemetry.qx;
      telemetry.refQy = telemetry.qy;
      telemetry.refQz = telemetry.qz;
      telemetry.refQw = telemetry.qw;
      telemetry.refCompassOffsetDeg = 0.0f; // Zeroes heading to 0.0 deg
      telemetry.hasRefQuat = true;
    } else if (cmd == 'c' || cmd == 'C') {
      telemetry.hasRefQuat = false; // Re-locks to absolute compass heading on next packet
    }
  }

  // Drain all incoming UDP packets so we always have the freshest 60 Hz sample
  int packetSize = udp.parsePacket();
  while (packetSize > 0) {
    int len = udp.read(udpBuffer, sizeof(udpBuffer) - 1);
    if (len > 0) {
      udpBuffer[len] = '\0';
      parseZigSimPacket(udpBuffer, len);
    }
    packetSize = udp.parsePacket();
  }

  // Output to Serial Plotter / Web Compass Plotter at 50 Hz (every 20 ms)
  if (now - lastPrintTime >= PRINT_INTERVAL_MS) {
    lastPrintTime = now;

    if (telemetry.hasQuat) {
      float heading = telemetry.quatHeadingDeg;
      float rad = heading * (PI / 180.0f);
      float arrowX = sinf(rad);
      float arrowY = cosf(rad);
      Serial.printf("Min:0.0,Heading:%.1f,Max:360.0,SinX:%.2f,CosY:%.2f\n",
                    heading, arrowX, arrowY);
    }
  }
}
