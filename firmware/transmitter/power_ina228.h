/*
  =============================================================================
  power_ina228.h — Modular 48V Battery & INA228 Power Telemetry Driver
  Supports:
    1. SIMULATED MODE (`trikeState.simBattery == true`):
       - Starts PAUSED at 100.0% SoC (54.60V, 0.00A) on boot.
       - Only drains SoC and applies the 10s sinusoidal load current/sag while
         `trikeState.raceRunning == true`!
    2. HARDWARE MODE (`trikeState.simBattery == false`):
       - Polls TI INA228 over I2C (GPIO 5 SDA, GPIO 6 SCL, addr 0x40)
  =============================================================================
*/

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "config.h"
#include "telemetry_types.h"

extern TrikeTelemetryState trikeState;

static unsigned long lastPowerUpdateMs = 0;
static bool ina228HardwarePresent      = false;

// Check if an INA228 is physically connected on the I2C bus
inline bool probeIna228Hardware() {
  Wire.beginTransmission(INA228_I2C_ADDR);
  return (Wire.endTransmission() == 0);
}

inline void initPowerSensor() {
  trikeState.simBattery = DEFAULT_SIM_INA228_POWER;
  lastPowerUpdateMs     = millis();

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  ina228HardwarePresent = probeIna228Hardware();
}

inline void resetBatterySimulation() {
  trikeState.voltageV = SIM_BATT_FULL_MV * 0.001f;
  trikeState.currentA = 0.0f;
  trikeState.powerW   = 0.0f;
  trikeState.energyWh = 0.0f;
  trikeState.socPct   = 100.0f;
}

inline void updatePowerSensor(unsigned long nowMs) {
  float dtSec = (nowMs - lastPowerUpdateMs) * 0.001f;
  if (dtSec <= 0.0f) return;
  if (dtSec > 0.5f) dtSec = 0.025f;
  lastPowerUpdateMs = nowMs;

  if (trikeState.simBattery || !ina228HardwarePresent) {
    // When PAUSED, freeze SoC drain and show 0.0 A motor load (open-circuit pack voltage)
    if (!trikeState.raceRunning) {
      float socFraction   = trikeState.socPct * 0.01f;
      float openCircuitMv = SIM_BATT_EMPTY_MV + socFraction * (SIM_BATT_FULL_MV - SIM_BATT_EMPTY_MV);
      trikeState.voltageV = openCircuitMv * 0.001f;
      trikeState.currentA = 0.0f;
      trikeState.powerW   = 0.0f;
      return;
    }

    // 1. Accelerated 5-minute (300s) drain cycle driven by activeSimTimeMs: 100.0% -> 10.0% SoC
    float elapsedDrainSec = trikeState.activeSimTimeMs * 0.001f;
    float cycleProgress   = fmodf(elapsedDrainSec / SIM_BATT_DRAIN_CYCLE_SEC, 1.0f);
    float baseSoc         = 100.0f - (cycleProgress * 90.0f);
    trikeState.socPct     = constrain(baseSoc, 10.0f, 100.0f);

    // 2. Sinusoidal Load Current (1.0 A idle up to 12.0 A at 10 mph peak speed)
    float speedRatio      = constrain(trikeState.speedMph / SIM_SPEED_MAX_MPH, 0.0f, 1.0f);
    float harmonicRippleA = 0.35f * sinf((2.0f * PI * elapsedDrainSec) / 3.3f);
    float currentA        = SIM_BATT_IDLE_AMPS + (SIM_BATT_PEAK_LOAD_AMPS * speedRatio) + harmonicRippleA;
    trikeState.currentA   = constrain(currentA, 0.5f, 25.0f);

    // 3. 48V Nominal Pack Open-Circuit Voltage + Internal Resistance Sag under Load
    float socFraction   = trikeState.socPct * 0.01f;
    float openCircuitMv = SIM_BATT_EMPTY_MV + socFraction * (SIM_BATT_FULL_MV - SIM_BATT_EMPTY_MV);
    float sagMv         = (trikeState.currentA * SIM_BATT_INTERNAL_RES_OHM) * 1000.0f;
    float loadedMv      = constrain(openCircuitMv - sagMv, SIM_BATT_EMPTY_MV, SIM_BATT_FULL_MV);

    trikeState.voltageV = loadedMv * 0.001f;
    trikeState.powerW   = trikeState.voltageV * trikeState.currentA;

    // 4. Integrate cumulative Energy Used (Wh = W * s / 3600)
    trikeState.energyWh += (trikeState.powerW * dtSec) / 3600.0f;
  } else {
    // Future-Proof Hardware I2C INA228 Register Read (VBUS = Reg 0x05)
    Wire.beginTransmission(INA228_I2C_ADDR);
    Wire.write(0x05);
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(INA228_I2C_ADDR, (uint8_t)3) == 3) {
      uint32_t rawVbus = ((uint32_t)Wire.read() << 16) | ((uint32_t)Wire.read() << 8) | Wire.read();
      rawVbus >>= 4;
      trikeState.voltageV = (rawVbus * 195.3125e-6f);
    }
    trikeState.powerW = trikeState.voltageV * trikeState.currentA;
    if (trikeState.raceRunning) {
      trikeState.energyWh += (trikeState.powerW * dtSec) / 3600.0f;
    }
  }
}
