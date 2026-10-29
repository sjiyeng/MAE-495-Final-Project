/*
  =============================================================================
  ASME e-HPVC Telemetry System - 60 Hz ZIG SIM (iPhone 17) -> LoRa Transmitter
  Target Hardware: Seeed Studio XIAO ESP32S3 + Wio-SX1262 (B2B Connector)
  iOS App Required: "ZIG SIM" (by 1-10, Inc. - Free on Apple App Store)
  Libraries Required:
    - RadioLib (by Jan Gromes) via Arduino Library Manager
    - Built-in ESP32 WiFi & WiFiUdp libraries
  =============================================================================
  
  HOW IT WORKS:
  1. Hosts Wi-Fi SoftAP "ASME-Trike-TX" (IP: 192.168.4.1, UDP Port: 50000).
  2. Receives 60 Hz Quaternion data from iPhone 17 running ZIG SIM (JSON or OSC).
  3. Computes orientation-independent 0-360° Heading (locks 3D mounting pose on
     the first packet; resettable via 'z' or 'c' in Serial).
  4. Transmits compact 9-byte binary telemetry packets at 60 Hz (every 16 ms)
     over 915 MHz LoRa (SF7, 500 kHz BW -> ~4.8 ms airtime) using non-blocking
     DIO1 hardware interrupts so Wi-Fi UDP reception is never blocked.
  =============================================================================
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <RadioLib.h>
#include <math.h>

// -----------------------------------------------------------------------------
// Pin Definitions for Seeed XIAO ESP32S3 + Wio-SX1262 B2B Connector
// -----------------------------------------------------------------------------
#define LORA_LDO_EN_PIN   38   // Power / RF switch enable on Wio-SX1262
#define LORA_NSS_PIN      41   // SPI Chip Select
#define LORA_DIO1_PIN     39   // Interrupt (DIO1)
#define LORA_NRST_PIN     42   // Hardware Reset
#define LORA_BUSY_PIN     40   // Busy status line

SX1262 radio = new Module(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_NRST_PIN, LORA_BUSY_PIN);

// -----------------------------------------------------------------------------
// RF Configuration Tuned for 60 Hz Ultra-Low Airtime (~4.8 ms per packet)
// -----------------------------------------------------------------------------
const float LORA_FREQ_MHZ = 915.0;     // US915 Band
const float LORA_BW_KHZ   = 500.0;     // 500 kHz Bandwidth for 60 Hz operation
const uint8_t LORA_SF     = 7;         // Spreading Factor 7
const uint8_t LORA_CR     = 5;         // Coding Rate 4/5
const uint8_t LORA_SYNC   = RADIOLIB_SX126X_SYNC_WORD_PRIVATE;
const int8_t  LORA_PWR    = 14;        // 14 dBm output power
const uint16_t LORA_PREAMBLE = 6;      // Short preamble for minimal airtime
const float   LORA_TCXO   = 1.6;       // 1.6V reference for Wio-SX1262 TCXO

// -----------------------------------------------------------------------------
// Compact 9-Byte Binary LoRa Packet (Must match Receiver exactly)
// -----------------------------------------------------------------------------
struct __attribute__((packed)) LoRaHeadingPacket {
  uint8_t  magic;         // 0xA5 packet identifier
  uint16_t seq;           // Rolling packet sequence counter
  uint16_t heading_x10;   // Heading * 10 (0 .. 3600 -> 0.0 .. 360.0 deg)
  int16_t  roll_x10;      // Roll * 10 (-1800 .. +1800 -> -180.0 .. +180.0 deg)
  int16_t  pitch_x10;     // Pitch * 10 (-900 .. +900 -> -90.0 .. +90.0 deg)
};

// Non-blocking LoRa TX state
volatile bool txDoneFlag = false;
bool radioBusy = false;
unsigned long txStartTimeMs = 0;
uint16_t packetSeq = 0;

#if defined(ESP32) || defined(ESP8266)
IRAM_ATTR
#endif
void onTxDone(void) {
  txDoneFlag = true;
}

// -----------------------------------------------------------------------------
// Wi-Fi Access Point & UDP Configuration (Matches ZIG SIM Defaults)
// -----------------------------------------------------------------------------
const char* AP_SSID     = "ASME-Trike-TX"; // Open Wi-Fi network
const uint16_t UDP_PORT = 50000;           // Default port in ZIG SIM app

WiFiUDP udp;
char udpBuffer[2048];

// -----------------------------------------------------------------------------
// Telemetry State Variables
// -----------------------------------------------------------------------------
struct PhoneTelemetry {
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
// Timing for 60 Hz LoRa Transmission & Serial Output (Every 16 ms)
// -----------------------------------------------------------------------------
unsigned long lastTxTime = 0;
const unsigned long TX_INTERVAL_MS = 16; // ~60 Hz (16.67 ms)

// -----------------------------------------------------------------------------
// Helper: Compute Initial Compass Heading from Any Phone Orientation
// -----------------------------------------------------------------------------
float computeInitialCompassHeading(float x, float y, float z, float w) {
  float yWorldX = 2.0f * (x * y - w * z);
  float yWorldY = 1.0f - 2.0f * (x * x + z * z);
  float yHorizMagSq = yWorldX * yWorldX + yWorldY * yWorldY;

  float negZWorldX = -2.0f * (x * z + w * y);
  float negZWorldY = -2.0f * (y * z - w * x);
  float zHorizMagSq = negZWorldX * negZWorldX + negZWorldY * negZWorldY;

  float fwdX = (yHorizMagSq >= zHorizMagSq) ? yWorldX : negZWorldX;
  float fwdY = (yHorizMagSq >= zHorizMagSq) ? yWorldY : negZWorldY;

  float yawDeg = atan2f(fwdX, fwdY) * (180.0f / PI);
  if (yawDeg < 0.0f) yawDeg += 360.0f;
  return yawDeg;
}

// -----------------------------------------------------------------------------
// Helper: Update Heading from Normalized Quaternion (x, y, z, w)
// -----------------------------------------------------------------------------
void updateQuaternionHeading(float x, float y, float z, float w) {
  float norm = sqrtf(x * x + y * y + z * z + w * w);
  if (norm < 0.0001f) return;
  x /= norm; y /= norm; z /= norm; w /= norm;

  if (!telemetry.hasRefQuat) {
    telemetry.refQx = x;
    telemetry.refQy = y;
    telemetry.refQz = z;
    telemetry.refQw = w;
    telemetry.refCompassOffsetDeg = computeInitialCompassHeading(x, y, z, w);
    telemetry.hasRefQuat = true;
  }

  // dQ_world = Q_current * conjugate(Q_ref)
  float rQx = -telemetry.refQx;
  float rQy = -telemetry.refQy;
  float rQz = -telemetry.refQz;
  float rQw =  telemetry.refQw;

  float dQw = w * rQw - x * rQx - y * rQy - z * rQz;
  float dQx = w * rQx + x * rQw + y * rQz - z * rQy;
  float dQy = w * rQy - x * rQz + y * rQw + z * rQx;
  float dQz = w * rQz + x * rQy - y * rQx + z * rQw;

  float sinr_cosp = 2.0f * (dQw * dQx + dQy * dQz);
  float cosr_cosp = 1.0f - 2.0f * (dQx * dQx + dQy * dQy);
  float roll = atan2f(sinr_cosp, cosr_cosp) * (180.0f / PI);

  float sinp = 2.0f * (dQw * dQy - dQz * dQx);
  float pitch = (fabsf(sinp) >= 1.0f) ? copysignf(90.0f, sinp) : (asinf(sinp) * (180.0f / PI));

  float siny_cosp = 2.0f * (dQw * dQz + dQx * dQy);
  float cosy_cosp = 1.0f - 2.0f * (dQy * dQy + dQz * dQz);
  float deltaYawDeg = atan2f(siny_cosp, cosy_cosp) * (180.0f / PI);

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
// Helper: Extract a Float Key Inside a JSON Substring
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
// Helper: Read Big-Endian IEEE-754 Float (OSC fallback)
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
// Parse Incoming ZIG SIM UDP Packet (Supports BOTH JSON and OSC Formats)
// -----------------------------------------------------------------------------
void parseZigSimPacket(const char* buf, int len) {
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

  for (int i = 0; i < len - 28; i++) {
    if (memcmp(&buf[i], "quaternion", 10) == 0) {
      for (int j = i + 10; j < i + 24 && j + 24 <= len; j++) {
        if (memcmp(&buf[j], ",ffff", 5) == 0) {
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

  // 1. Start Wi-Fi SoftAP for ZIG SIM (192.168.4.1:50000)
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  delay(100);
  udp.begin(UDP_PORT);

  // 2. Enable power / RF switch to Wio-SX1262
  pinMode(LORA_LDO_EN_PIN, OUTPUT);
  digitalWrite(LORA_LDO_EN_PIN, HIGH);
  delay(50);

  // 3. Configure TCXO & Initialize SX1262 for 60 Hz (915 MHz, BW 500 kHz, SF7)
  radio.tcxoVoltage = LORA_TCXO;
  int state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC, LORA_PWR, LORA_PREAMBLE);
  if (state != RADIOLIB_ERR_NONE) {
    while (true) {
      Serial.printf("LoRa_Init_Failed:%d\n", state);
      delay(1000);
    }
  }

  // 4. Attach non-blocking TX completion interrupt on DIO1
  radio.setDio1Action(onTxDone);
}

void loop() {
  unsigned long now = millis();

  // 1. Handle Serial 'z' (Zero) and 'c' (Compass Reset) commands
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'z' || cmd == 'Z') {
      telemetry.refQx = telemetry.qx;
      telemetry.refQy = telemetry.qy;
      telemetry.refQz = telemetry.qz;
      telemetry.refQw = telemetry.qw;
      telemetry.refCompassOffsetDeg = 0.0f;
      telemetry.hasRefQuat = true;
    } else if (cmd == 'c' || cmd == 'C') {
      telemetry.hasRefQuat = false;
    }
  }

  // 2. Drain all incoming 60 Hz ZIG SIM UDP packets from iPhone 17
  int packetSize = udp.parsePacket();
  while (packetSize > 0) {
    int len = udp.read(udpBuffer, sizeof(udpBuffer) - 1);
    if (len > 0) {
      udpBuffer[len] = '\0';
      parseZigSimPacket(udpBuffer, len);
    }
    packetSize = udp.parsePacket();
  }

  // 3. Check if previous non-blocking LoRa transmission finished
  if (txDoneFlag) {
    txDoneFlag = false;
    radio.finishTransmit();
    radioBusy = false;
  } else if (radioBusy && (now - txStartTimeMs > 50)) {
    // Safety watchdog in case DIO1 interrupt was missed
    radio.finishTransmit();
    radioBusy = false;
  }

  // 4. Transmit over LoRa & print to Serial at 60 Hz (every 16 ms)
  if (now - lastTxTime >= TX_INTERVAL_MS) {
    lastTxTime = now;

    if (telemetry.hasQuat) {
      float heading = telemetry.quatHeadingDeg;

      // Launch non-blocking LoRa packet transmission if radio is ready
      if (!radioBusy) {
        packetSeq++;
        LoRaHeadingPacket pkt;
        pkt.magic       = 0xA5;
        pkt.seq         = packetSeq;
        pkt.heading_x10 = (uint16_t)(heading * 10.0f + 0.5f) % 3600;
        pkt.roll_x10    = (int16_t)(telemetry.rollDeg * 10.0f);
        pkt.pitch_x10   = (int16_t)(telemetry.pitchDeg * 10.0f);

        int state = radio.startTransmit((uint8_t*)&pkt, sizeof(pkt));
        if (state == RADIOLIB_ERR_NONE) {
          radioBusy = true;
          txStartTimeMs = now;
        }
      }

      // Also output to Transmitter USB Serial for local Plotter / Compass verification
      float rad = heading * (PI / 180.0f);
      float arrowX = sinf(rad);
      float arrowY = cosf(rad);
      Serial.printf("Min:0.0,Heading:%.1f,Max:360.0,SinX:%.2f,CosY:%.2f\n",
                    heading, arrowX, arrowY);
    }
  }
}
