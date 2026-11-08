/*
  =============================================================================
  imu_zig_sim.h — 60 Hz iPhone ZIG SIM Wi-Fi UDP Receiver & 3D Quaternion Math
  Handles:
    1. "ASME-Trike-TX" SoftAP and station Wi-Fi RSSI monitoring
    2. ZIG SIM UDP packet parsing (supports both JSON and OSC binary formats)
    3. Arbitrary 3D mounting orientation reference lock (Roll, Pitch, World Yaw)
    4. Lateral cornering G-force estimation from yaw rate * wheel speed
  =============================================================================
*/

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>
#include <math.h>
#include "config.h"
#include "telemetry_types.h"

extern TrikeTelemetryState trikeState;

static WiFiUDP zigUdp;
static char zigUdpBuffer[2048];
static unsigned long lastQuatMathMs = 0;

// -----------------------------------------------------------------------------
// Compute Initial Compass Heading from Any Phone Mounting Orientation
// Projects Phone +Y (top edge) and Phone -Z (rear camera) onto the horizontal
// World XY plane and chooses whichever axis is closer to horizontal.
// -----------------------------------------------------------------------------
inline float computeInitialCompassHeading(float x, float y, float z, float w) {
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
// Update Attitude & World Yaw from Normalized Quaternion (x, y, z, w)
// -----------------------------------------------------------------------------
inline void updateQuaternionAttitude(float x, float y, float z, float w) {
  float norm = sqrtf(x * x + y * y + z * z + w * w);
  if (norm < 0.0001f) return;
  x /= norm; y /= norm; z /= norm; w /= norm;

  unsigned long nowMs = millis();

  if (!trikeState.hasRefQuat) {
    trikeState.refQx = x;
    trikeState.refQy = y;
    trikeState.refQz = z;
    trikeState.refQw = w;
    trikeState.refCompassOffsetDeg = 0.0f; // 0.0 deg = Initial Forward Pose
    trikeState.hasRefQuat = true;
    lastQuatMathMs = nowMs;
  }

  // dQ_world = Q_current * conjugate(Q_ref)
  float rQx = -trikeState.refQx;
  float rQy = -trikeState.refQy;
  float rQz = -trikeState.refQz;
  float rQw =  trikeState.refQw;

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

  float heading = fmodf(trikeState.refCompassOffsetDeg - deltaYawDeg + 360.0f, 360.0f);
  if (heading < 0.0f) heading += 360.0f;

  // Compute yaw rate and lateral cornering acceleration (a_lat = v * omega / g)
  float dtSec = (nowMs - lastQuatMathMs) * 0.001f;
  if (dtSec >= 0.005f && dtSec < 0.5f && trikeState.hasQuat) {
    float dHeading = heading - trikeState.prevHeadingDeg;
    if (dHeading > 180.0f)  dHeading -= 360.0f;
    if (dHeading < -180.0f) dHeading += 360.0f;
    float instYawRate = dHeading / dtSec;
    trikeState.yawRateDegPerS = 0.7f * trikeState.yawRateDegPerS + 0.3f * instYawRate;

    float omegaRadS = trikeState.yawRateDegPerS * (PI / 180.0f);
    float latAccelFts2 = trikeState.speedFts * omegaRadS;
    float gFromYaw = latAccelFts2 / 32.174f;
    float gFromRoll = sinf(roll * (PI / 180.0f)) * 0.45f;
    trikeState.lateralG = constrain(gFromYaw + gFromRoll, -3.0f, 3.0f);
  }
  lastQuatMathMs = nowMs;
  trikeState.prevHeadingDeg = heading;

  trikeState.qx = x;
  trikeState.qy = y;
  trikeState.qz = z;
  trikeState.qw = w;
  trikeState.rollDeg    = roll;
  trikeState.pitchDeg   = pitch;
  trikeState.headingDeg = heading;
  trikeState.hasQuat    = true;
  trikeState.quatCount++;
  trikeState.lastUdpTimeMs = nowMs;
  trikeState.udpPacketsThisSec++;
  if (trikeState.measuredZigHz == 0) {
    trikeState.measuredZigHz = 60; // Immediate non-zero rate on first packet before 1s timer ticks
  }
}

inline void zeroCurrentHeading() {
  trikeState.refQx = trikeState.qx;
  trikeState.refQy = trikeState.qy;
  trikeState.refQz = trikeState.qz;
  trikeState.refQw = trikeState.qw;
  trikeState.refCompassOffsetDeg = 0.0f;
  trikeState.hasRefQuat = true;
}

inline void resetCompassHeading() {
  trikeState.hasRefQuat = false;
}

inline bool extractJsonFloat(const char* sectionStart, const char* sectionEnd, const char* key, float& outVal) {
  const char* p = strstr(sectionStart, key);
  if (!p || p >= sectionEnd) return false;
  p += strlen(key);
  while (p < sectionEnd && (*p == ' ' || *p == '\t' || *p == ':')) p++;
  if (p >= sectionEnd) return false;
  outVal = strtof(p, nullptr);
  return true;
}

inline float readFloatBE(const uint8_t* ptr) {
  uint32_t raw = ((uint32_t)ptr[0] << 24) |
                 ((uint32_t)ptr[1] << 16) |
                 ((uint32_t)ptr[2] << 8)  |
                 ((uint32_t)ptr[3]);
  float val;
  memcpy(&val, &raw, sizeof(float));
  return val;
}

inline void parseZigSimPacket(const char* buf, int len) {
  // 1. JSON Format: {"sensordata":{"quaternion":{"x":...,"y":...,"z":...,"w":...}}}
  const char* quatSection = strstr(buf, "\"quaternion\"");
  if (quatSection) {
    const char* closeBrace = strchr(quatSection, '}');
    if (closeBrace) {
      float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
      if (extractJsonFloat(quatSection, closeBrace, "\"x\"", x) &&
          extractJsonFloat(quatSection, closeBrace, "\"y\"", y) &&
          extractJsonFloat(quatSection, closeBrace, "\"z\"", z) &&
          extractJsonFloat(quatSection, closeBrace, "\"w\"", w)) {
        updateQuaternionAttitude(x, y, z, w);
        return;
      }
    }
  }

  // 2. OSC Binary Format Fallback
  for (int i = 0; i < len - 28; i++) {
    if (memcmp(&buf[i], "quaternion", 10) == 0) {
      for (int j = i + 10; j < i + 24 && j + 24 <= len; j++) {
        if (memcmp(&buf[j], ",ffff", 5) == 0) {
          const uint8_t* fPtr = (const uint8_t*)&buf[j + 8];
          float x = readFloatBE(fPtr + 0);
          float y = readFloatBE(fPtr + 4);
          float z = readFloatBE(fPtr + 8);
          float w = readFloatBE(fPtr + 12);
          updateQuaternionAttitude(x, y, z, w);
          return;
        }
      }
    }
  }
}

inline void initImuZigSim() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(TX_AP_SSID);
  WiFi.setSleep(false);
  delay(100);
  zigUdp.begin(ZIG_UDP_PORT);
}

inline void pollImuZigSim() {
  int packetSize = zigUdp.parsePacket();
  while (packetSize > 0) {
    int len = zigUdp.read(zigUdpBuffer, sizeof(zigUdpBuffer) - 1);
    if (len > 0) {
      zigUdpBuffer[len] = '\0';
      parseZigSimPacket(zigUdpBuffer, len);
    }
    packetSize = zigUdp.parsePacket();
  }
}

inline void updatePhoneWifiDiagnostics1Hz() {
  uint8_t instHz = (trikeState.udpPacketsThisSec > 255) ? 255 : (uint8_t)trikeState.udpPacketsThisSec;
  trikeState.udpPacketsThisSec = 0;

  // Keep a smooth non-zero rate while packets have arrived within the last 3 seconds
  unsigned long nowMs = millis();
  if (instHz > 0) {
    trikeState.measuredZigHz = instHz;
  } else if (trikeState.lastUdpTimeMs == 0 || (nowMs - trikeState.lastUdpTimeMs > 3000)) {
    trikeState.measuredZigHz = 0;
  }

  wifi_sta_list_t staList;
  if (esp_wifi_ap_get_sta_list(&staList) == ESP_OK && staList.num > 0) {
    trikeState.phoneWifiConnected = true;
    trikeState.phoneWifiRssi      = staList.sta[0].rssi;
  } else if (trikeState.lastUdpTimeMs > 0 && (nowMs - trikeState.lastUdpTimeMs <= 3000)) {
    // If UDP packets are actively arriving, the phone is definitely connected
    trikeState.phoneWifiConnected = true;
  } else {
    trikeState.phoneWifiConnected = false;
    trikeState.phoneWifiRssi      = -127;
  }
}
