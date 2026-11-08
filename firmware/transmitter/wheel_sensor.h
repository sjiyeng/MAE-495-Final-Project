/*
  =============================================================================
  wheel_sensor.h — Modular Wheel Speed & Distance Driver
  Supports:
    1. SIMULATED MODE (`trikeState.simWheelSpeed == true`):
       - Starts PAUSED (0.0 mph, 0 RPM) on boot.
       - Only advances the 10-second sinusoidal speed cycle (0.0 -> 10.0 -> 0.0 mph)
         and integrates distance while `trikeState.raceRunning == true`!
    2. HARDWARE MODE (`trikeState.simWheelSpeed == false`):
       - IRAM hardware interrupt on GPIO 1 (Header D0) for the HiLetgo 3.3V
         Hall-effect sensor with microsecond debounce and timeout zeroing.
  =============================================================================
*/

#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "telemetry_types.h"

extern TrikeTelemetryState trikeState;

// Hardware ISR variables (ready for when the physical Hall sensor is wired)
static volatile uint32_t hallPulseCount      = 0;
static volatile uint32_t lastHallPulseUs     = 0;
static volatile uint32_t latestPulsePeriodUs = 0;
static uint32_t prevHallPulseCount           = 0;
static unsigned long lastWheelUpdateMs       = 0;

#if defined(ESP32)
IRAM_ATTR
#endif
void onHallSensorPulse() {
  uint32_t nowUs = micros();
  uint32_t dtUs = nowUs - lastHallPulseUs;
  if (dtUs >= HALL_DEBOUNCE_US) {
    latestPulsePeriodUs = dtUs;
    lastHallPulseUs = nowUs;
    hallPulseCount++;
  }
}

inline void initWheelSensor() {
  trikeState.simWheelSpeed   = DEFAULT_SIM_WHEEL_SPEED;
  trikeState.wheelDiameterIn = DEFAULT_WHEEL_DIAMETER_IN;
  lastWheelUpdateMs          = millis();

  // Always configure GPIO 1 so hardware mode can be toggled live without rebooting
  pinMode(HALL_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(HALL_SENSOR_PIN), onHallSensorPulse, RISING);
}

inline void resetWheelSensorState() {
  trikeState.speedMph        = 0.0f;
  trikeState.speedFts        = 0.0f;
  trikeState.wheelRpm        = 0.0f;
  trikeState.totalDistanceFt = 0.0f;
}

inline void updateWheelSensor(unsigned long nowMs) {
  unsigned long elapsedMs = nowMs - lastWheelUpdateMs;
  if (elapsedMs == 0) return;
  if (elapsedMs > 500) elapsedMs = 25;
  lastWheelUpdateMs = nowMs;
  float dtSec = elapsedMs * 0.001f;

  // If race timer is PAUSED, hold wheel speed at 0.0 mph and freeze wave phase
  if (!trikeState.raceRunning) {
    trikeState.speedMph = 0.0f;
    trikeState.speedFts = 0.0f;
    trikeState.wheelRpm = 0.0f;
    return;
  }

  // Advance active race simulation clock only while running
  trikeState.activeSimTimeMs += elapsedMs;

  float circumferenceFt = (PI * trikeState.wheelDiameterIn) / 12.0f;

  if (trikeState.simWheelSpeed) {
    // 10-second sinusoidal cycle between 0.0 mph and 10.0 mph:
    // At active t = 0s -> 0 mph, t = 5s -> 10 mph, t = 10s -> 0 mph
    float tSec   = trikeState.activeSimTimeMs * 0.001f;
    float phase  = (2.0f * PI * tSec) / SIM_SPEED_PERIOD_SEC;
    float midMph = 0.5f * (SIM_SPEED_MIN_MPH + SIM_SPEED_MAX_MPH);
    float ampMph = 0.5f * (SIM_SPEED_MAX_MPH - SIM_SPEED_MIN_MPH);

    float simMph = midMph - ampMph * cosf(phase);
    simMph = constrain(simMph, SIM_SPEED_MIN_MPH, SIM_SPEED_MAX_MPH);

    trikeState.speedMph = simMph;
    trikeState.speedFts = simMph * 1.4666667f;
    trikeState.wheelRpm = (circumferenceFt > 0.01f)
                            ? ((trikeState.speedFts * 60.0f) / circumferenceFt)
                            : 0.0f;
    trikeState.totalDistanceFt += trikeState.speedFts * dtSec;
  } else {
    // Hardware Hall Sensor Mode (GPIO 1 Rising Interrupt)
    noInterrupts();
    uint32_t pulses   = hallPulseCount;
    uint32_t periodUs = latestPulsePeriodUs;
    uint32_t lastUs   = lastHallPulseUs;
    interrupts();

    uint32_t newPulses = pulses - prevHallPulseCount;
    prevHallPulseCount = pulses;

    if (newPulses > 0) {
      trikeState.totalDistanceFt += (newPulses / (float)HALL_PULSES_PER_REV) * circumferenceFt;
    }

    uint32_t ageMs = (micros() - lastUs) / 1000UL;
    if (pulses == 0 || ageMs > WHEEL_TIMEOUT_MS || periodUs == 0) {
      trikeState.wheelRpm = 0.0f;
      trikeState.speedFts = 0.0f;
      trikeState.speedMph = 0.0f;
    } else {
      float revPerSec = 1000000.0f / ((float)periodUs * (float)HALL_PULSES_PER_REV);
      trikeState.wheelRpm = revPerSec * 60.0f;
      trikeState.speedFts = revPerSec * circumferenceFt;
      trikeState.speedMph = trikeState.speedFts * 0.6818182f;
    }
  }
}
