/*
  =============================================================================
  imu_math.h — 3D Quaternion Orientation Lock & Heading Math
  =============================================================================
*/

#pragma once
#include <Arduino.h>
#include <math.h>

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

  // Diagnostics
  unsigned long lastUdpTimeMs = 0;
  uint16_t udpPacketsThisSec  = 0;
  uint8_t  measuredZigHz      = 0;
  bool     phoneWifiConnected = false;
  int8_t   phoneWifiRssi      = -127;
};

extern PhoneTelemetry telemetry;

// -----------------------------------------------------------------------------
// Compute Initial Compass Heading from Any Phone Orientation
// Projects both Phone +Y (top edge) and Phone -Z (rear camera) onto the
// World Horizontal (XY) plane and picks whichever axis is more horizontal.
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
// Update Heading from Normalized Quaternion (x, y, z, w)
// Uses first-packet reference pose: dQ_world = Q_current * conjugate(Q_ref)
// -----------------------------------------------------------------------------
inline void updateQuaternionHeading(float x, float y, float z, float w) {
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
  telemetry.lastUdpTimeMs = millis();
  telemetry.udpPacketsThisSec++;
}

inline void zeroCurrentHeading() {
  telemetry.refQx = telemetry.qx;
  telemetry.refQy = telemetry.qy;
  telemetry.refQz = telemetry.qz;
  telemetry.refQw = telemetry.qw;
  telemetry.refCompassOffsetDeg = 0.0f;
  telemetry.hasRefQuat = true;
}

inline void resetCompassHeading() {
  telemetry.hasRefQuat = false;
}
