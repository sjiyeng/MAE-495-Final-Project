/*
  =============================================================================
  flash_logger.h — 60 Hz Local PSRAM Telemetry Logger (2.5 Hours = 540,000 Samples)
                   + 1 Hz Non-Volatile LittleFS Checkpoint Logger
  -----------------------------------------------------------------------------
  RAM Capacity Verification (Seeed Studio XIAO ESP32S3):
    - Total Onboard Octal PSRAM : 8,388,608 bytes (8.00 MB)
    - 2.5-Hour Race @ 60 Hz     : 9,000 sec * 60 Hz = 540,000 samples
    - Record Size               : sizeof(RamLogRecord60Hz) = 12 bytes
    - Allocated Buffer Size     : 540,000 * 12 B = 6,480,000 bytes (6.18 MB)
    - Free PSRAM Headroom       : ~1,908,608 bytes (1.82 MB free PSRAM + ~285 KB SRAM)
  =============================================================================
*/

#pragma once
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include "config.h"
#include "telemetry_types.h"

extern TrikeTelemetryState trikeState;

static const char* TX_LOG_PATH       = "/tx_log.bin";
static unsigned long lastFlashLogMs  = 0;
static unsigned long lastRamLogUs    = 0;
static RamLogRecord60Hz* txRamBuffer = nullptr;

inline void initFlashLogger() {
  // 1. Verify & Pre-Allocate 6.18 MB (540,000 samples @ 60 Hz = 2.5 Hours) in 8 MB OPI PSRAM
  size_t requiredBytes = (size_t)MAX_RAM_LOG_SAMPLES * sizeof(RamLogRecord60Hz); // 6,480,000 bytes
  if (psramFound() && ESP.getFreePsram() >= requiredBytes) {
    txRamBuffer = (RamLogRecord60Hz*)ps_malloc(requiredBytes);
    if (txRamBuffer != nullptr) {
      trikeState.ramLogReady      = true;
      trikeState.txRamCapacity    = MAX_RAM_LOG_SAMPLES;
      trikeState.txFreePsramBytes = ESP.getFreePsram();
    }
  }

  // Graceful fallback if compiled without Tools -> PSRAM: "OPI PSRAM"
  if (txRamBuffer == nullptr) {
    size_t availPsram = ESP.getFreePsram();
    if (availPsram >= sizeof(RamLogRecord60Hz) * 60000UL) {
      uint32_t cap = (uint32_t)((availPsram - 65536UL) / sizeof(RamLogRecord60Hz));
      txRamBuffer = (RamLogRecord60Hz*)ps_malloc(cap * sizeof(RamLogRecord60Hz));
      if (txRamBuffer != nullptr) {
        trikeState.ramLogReady   = true;
        trikeState.txRamCapacity = cap;
      }
    } else {
      // Internal SRAM ring buffer fallback (12,000 samples = 144 KB) if OPI PSRAM is disabled
      uint32_t sramCap = 12000UL;
      txRamBuffer = (RamLogRecord60Hz*)malloc(sramCap * sizeof(RamLogRecord60Hz));
      if (txRamBuffer != nullptr) {
        trikeState.ramLogReady   = true;
        trikeState.txRamCapacity = sramCap;
      }
    }
    trikeState.txFreePsramBytes = ESP.getFreePsram();
  }
  trikeState.txRamSamples60Hz = 0;
  trikeState.flashReady       = trikeState.ramLogReady;
  trikeState.txRecordsLogged  = 0;

  lastFlashLogMs = millis();
  lastRamLogUs   = micros();
}

inline void clearFlashLog() {
  trikeState.txRamSamples60Hz = 0;
  trikeState.txRecordsLogged  = 0;
}

// High-Rate 60 Hz (16.67 ms) Local RAM Logger for Full 2.5-Hour Race (540,000 samples)
inline void updateRamLogger60Hz(unsigned long nowUs) {
  if ((nowUs - lastRamLogUs) < RAM_LOG_INTERVAL_US) return;
  // Prevent burst catch-up if delayed; advance by exact 16,667 us period
  if ((nowUs - lastRamLogUs) > (RAM_LOG_INTERVAL_US * 4UL)) {
    lastRamLogUs = nowUs;
  } else {
    lastRamLogUs += RAM_LOG_INTERVAL_US;
  }

  if (!trikeState.raceRunning || !trikeState.ramLogReady || txRamBuffer == nullptr) return;

  uint32_t writeIdx = trikeState.txRamSamples60Hz;
  if (writeIdx >= trikeState.txRamCapacity) {
    // If 540,000 PSRAM capacity is reached (>2.5 hours), wrap safely in ring buffer
    writeIdx = trikeState.txRamSamples60Hz % trikeState.txRamCapacity;
  }

  RamLogRecord60Hz& sample = txRamBuffer[writeIdx];
  sample.speed_mph_x100 = (uint16_t)(constrain(trikeState.speedMph, 0.0f, 99.9f) * 100.0f + 0.5f);
  sample.yaw_deg_x10    = (uint16_t)(trikeState.headingDeg * 10.0f + 0.5f) % 3600;
  sample.pos_x_ft_x10   = (int16_t)constrain((int)(trikeState.posXFt * 10.0f), -32767, 32767);
  sample.pos_y_ft_x10   = (int16_t)constrain((int)(trikeState.posYFt * 10.0f), -32767, 32767);
  sample.roll_deg       = (int8_t)constrain((int)roundf(trikeState.rollDeg), -127, 127);
  sample.pitch_deg      = (int8_t)constrain((int)roundf(trikeState.pitchDeg), -127, 127);
  sample.voltage_dV     = (uint8_t)constrain((int)(trikeState.voltageV * 4.0f + 0.5f), 0, 255);
  sample.current_dA     = (uint8_t)constrain((int)(trikeState.currentA * 10.0f + 0.5f), 0, 255);

  if (trikeState.txRamSamples60Hz < MAX_RAM_LOG_SAMPLES) {
    trikeState.txRamSamples60Hz++;
  }
}

inline void updateFlashLogger1Hz(unsigned long nowMs) {
  if (nowMs - lastFlashLogMs < FLASH_LOG_INTERVAL_MS) return;
  lastFlashLogMs = nowMs;

  if (!trikeState.raceRunning) return;
  if (!trikeState.flashReady) return;
  if (trikeState.txRecordsLogged >= MAX_FLASH_RECORDS) return;

  FlashLogRecord rec;
  rec.seq_num        = trikeState.txRecordsLogged + 1;
  rec.timestamp_s    = (uint16_t)(trikeState.activeSimTimeMs / 1000UL);
  rec.speed_mph_x100 = (uint16_t)(constrain(trikeState.speedMph, 0.0f, 99.9f) * 100.0f + 0.5f);
  rec.wheel_rpm_x10  = (uint16_t)(constrain(trikeState.wheelRpm, 0.0f, 6000.0f) * 10.0f + 0.5f);
  rec.roll_x10       = (int16_t)(trikeState.rollDeg * 10.0f);
  rec.pitch_x10      = (int16_t)(trikeState.pitchDeg * 10.0f);
  rec.yaw_deg_x10    = (uint16_t)(trikeState.headingDeg * 10.0f + 0.5f) % 3600;
  rec.voltage_dV     = (uint8_t)constrain((int)(trikeState.voltageV * 4.0f + 0.5f), 0, 255);
  rec.soc_pct        = (uint8_t)constrain((int)(trikeState.socPct + 0.5f), 0, 100);

  File f = LittleFS.open(TX_LOG_PATH, FILE_APPEND);
  if (f) {
    if (f.write((const uint8_t*)&rec, sizeof(rec)) == sizeof(rec)) {
      trikeState.txRecordsLogged++;
    }
    f.close();
  }
}
