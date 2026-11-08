/*
  =============================================================================
  kalman_nav.h — 2D Dead-Reckoning & Variance-Weighted Kalman Filter
  Integrates wheel speed (ft/s) along the live Phone IMU heading (deg) to track
  the trike's 2D coordinates (posXFt, posYFt) and uncertainty radius.
  =============================================================================
*/

#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "telemetry_types.h"

extern TrikeTelemetryState trikeState;

static unsigned long lastNavUpdateMs = 0;

inline void initKalmanNav() {
  lastNavUpdateMs          = millis();
  trikeState.posXFt        = 0.0f;
  trikeState.posYFt        = 0.0f;
  trikeState.uncertaintyFt = 1.5f;
}

inline void snapNavToStartPin() {
  trikeState.posXFt        = 0.0f;
  trikeState.posYFt        = 0.0f;
  trikeState.uncertaintyFt = 1.5f;
}

inline void updateKalmanNav(unsigned long nowMs) {
  float dtSec = (nowMs - lastNavUpdateMs) * 0.001f;
  if (dtSec <= 0.0f) return;
  if (dtSec > 0.5f) dtSec = 0.025f;
  lastNavUpdateMs = nowMs;

  float dsFt = trikeState.speedFts * dtSec;
  float headingRad = trikeState.headingDeg * (PI / 180.0f);

  // +X = East, +Y = North (matches compass convention 0 deg = North, 90 deg = East)
  trikeState.posXFt += dsFt * sinf(headingRad);
  trikeState.posYFt += dsFt * cosf(headingRad);

  // Dead-reckoning uncertainty grows slowly (~1.5% of distance traveled) until Loop-Closure
  trikeState.uncertaintyFt = constrain(trikeState.uncertaintyFt + (dsFt * 0.008f), 1.5f, 35.0f);
}
