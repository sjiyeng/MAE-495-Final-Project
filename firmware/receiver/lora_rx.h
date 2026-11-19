/*
  =============================================================================
  lora_rx.h — Core 1 Wio-SX1262 DIO1 Receiver, Command Sync & Dead-Reckoning
  =============================================================================
*/

#pragma once
#include <Arduino.h>
#include <RadioLib.h>
#include <FS.h>
#include <LittleFS.h>
#include <math.h>
#include "config.h"
#include "telemetry_types.h"

extern ReceiverState rxState;

struct LowSpikeSX1262 : public SX1262 {
  using SX1262::SX1262;
  int16_t configureLowCurrentSpikes(int8_t powerDbm) {
    // 1. Force high-efficiency internal DC-DC buck converter (~50% lower current than LDO)
    int16_t state = setRegulatorDCDC();
    // 2. Use power-saving (non-boosted) LNA gain mode in RX (~4.2 mA vs ~9.6 mA) & avoid close-range overload
    state |= setRxBoostedGainMode(false);
    // 3. Slow PA ramp-up time to 800 us (vs 40 us default) to eliminate steep di/dt rail dips
    int8_t rawPwr = constrain((int)powerDbm, -9, 22);
    state |= SX126x::setTxParams(rawPwr, RADIOLIB_SX126X_PA_RAMP_800U);
    return state;
  }
};

static LowSpikeSX1262 radio = new Module(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_NRST_PIN, LORA_BUSY_PIN);
static volatile bool receivedFlag    = false;
static bool rxFlashReady             = false;
static RamLogRecord60Hz* rxRamBuffer = nullptr;
static unsigned long lastRxRamLogUs  = 0;
static unsigned long lastWatchdogMs  = 0;

#if defined(ESP32)
IRAM_ATTR
#endif
void onLoRaPacketReceived(void) {
  receivedFlag = true;
}

inline bool configureLoRaReceiverRadio() {
  digitalWrite(LORA_LDO_EN_PIN, HIGH);
  radio.tcxoVoltage = LORA_TCXO_VOLTS;
  int state = radio.begin(
    LORA_FREQ_MHZ,
    LORA_BW_KHZ,
    LORA_SF,
    LORA_CR,
    RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
    LORA_PWR_DBM,
    LORA_PREAMBLE_LEN
  );
  if (state == RADIOLIB_ERR_NONE) {
    radio.configureLowCurrentSpikes(LORA_PWR_DBM);
    radio.setDio1Action(onLoRaPacketReceived);
    receivedFlag = false;
    radio.startReceive();
    return true;
  }
  return false;
}

inline void initLoRaReceiver() {
  // 1. Pre-allocate 6.18 MB (540,000 samples = 2.5 Hours @ 60 Hz) in 8 MB OPI PSRAM
  size_t requiredBytes = (size_t)MAX_RAM_LOG_SAMPLES * sizeof(RamLogRecord60Hz); // 6,480,000 bytes
  if (psramFound() && ESP.getFreePsram() >= requiredBytes) {
    rxRamBuffer = (RamLogRecord60Hz*)ps_malloc(requiredBytes);
    if (rxRamBuffer != nullptr) {
      rxState.rxRamLogReady = true;
      rxState.rxRamCapacity = MAX_RAM_LOG_SAMPLES;
    }
  }
  if (rxRamBuffer == nullptr) {
    size_t availPsram = ESP.getFreePsram();
    if (availPsram >= sizeof(RamLogRecord60Hz) * 60000UL) {
      uint32_t cap = (uint32_t)((availPsram - 65536UL) / sizeof(RamLogRecord60Hz));
      rxRamBuffer = (RamLogRecord60Hz*)ps_malloc(cap * sizeof(RamLogRecord60Hz));
      if (rxRamBuffer != nullptr) {
        rxState.rxRamLogReady = true;
        rxState.rxRamCapacity = cap;
      }
    } else {
      uint32_t sramCap = 12000UL;
      rxRamBuffer = (RamLogRecord60Hz*)malloc(sramCap * sizeof(RamLogRecord60Hz));
      if (rxRamBuffer != nullptr) {
        rxState.rxRamLogReady = true;
        rxState.rxRamCapacity = sramCap;
      }
    }
  }
  lastRxRamLogUs = micros();

  pinMode(LORA_LDO_EN_PIN, OUTPUT);
  digitalWrite(LORA_LDO_EN_PIN, HIGH);
  pinMode(LORA_DIO1_PIN, INPUT);
  delay(50);

  if (!configureLoRaReceiverRadio()) {
    while (true) {
      Serial.println("LoRa_RX_Init_Failed");
      delay(1000);
    }
  }
  lastWatchdogMs = millis();
}

inline void resetPacketLossStats() {
  rxState.totalRxPackets = 0;
  rxState.lostPackets    = 0;
  rxState.hasFirstPacket = false;
}

inline void snapReceiverPositionToStartPin() {
  rxState.posXFt        = 0.0f;
  rxState.posYFt        = 0.0f;
  rxState.lapDistanceFt = 0.0f;
  rxState.uncertaintyFt = 1.5f;
}

inline void setRaceRunningState(bool running) {
  rxState.desiredRaceRunning = running;
  if (!running) {
    rxState.speedMph = 0.0f;
    rxState.speedFts = 0.0f;
    rxState.wheelRpm = 0.0f;
    rxState.currentA = 0.0f;
    rxState.powerW   = 0.0f;
  }
}

inline void triggerFullRaceReset() {
  rxState.desiredRaceRunning = false;
  rxState.activeRaceTimeMs   = 0;
  rxState.speedMph           = 0.0f;
  rxState.speedFts           = 0.0f;
  rxState.wheelRpm           = 0.0f;
  rxState.totalDistanceFt    = 0.0f;
  rxState.prevTxDistanceFt   = 0.0f;
  rxState.hasInitialDist     = false;
  rxState.lapDistanceFt      = 0.0f;
  rxState.posXFt             = 0.0f;
  rxState.posYFt             = 0.0f;
  rxState.hasInitialTxPos    = false;
  rxState.uncertaintyFt      = 1.5f;
  rxState.voltageV           = 54.6f;
  rxState.currentA           = 0.0f;
  rxState.powerW             = 0.0f;
  rxState.energyWh           = 0.0f;
  rxState.socPct             = 100.0f;
  // Preserve rxState.rxFlashRecords across race stop so logged 60Hz samples never disappear
  rxState.resetSeq++;
  rxState.pendingResetBursts = 4; // Send reset command over 4 slots for guaranteed delivery
}

// High-Rate 60 Hz (16.67 ms) Local RAM Logger on Receiver (540,000 samples = 2.5 Hours)
inline void updateReceiverRamLog60Hz(unsigned long nowUs) {
  if ((nowUs - lastRxRamLogUs) < RAM_LOG_INTERVAL_US) return;
  if ((nowUs - lastRxRamLogUs) > (RAM_LOG_INTERVAL_US * 4UL)) {
    lastRxRamLogUs = nowUs;
  } else {
    lastRxRamLogUs += RAM_LOG_INTERVAL_US;
  }

  if (!rxState.desiredRaceRunning || !rxState.rxRamLogReady || rxRamBuffer == nullptr) return;
  if (rxState.totalRxPackets == 0) return;

  uint32_t writeIdx = rxState.rxFlashRecords;
  if (writeIdx >= rxState.rxRamCapacity) {
    writeIdx = rxState.rxFlashRecords % rxState.rxRamCapacity;
  }

  RamLogRecord60Hz& sample = rxRamBuffer[writeIdx];
  sample.speed_mph_x100 = (uint16_t)(constrain(rxState.speedMph, 0.0f, 99.9f) * 100.0f + 0.5f);
  sample.yaw_deg_x10    = (uint16_t)(rxState.displayHeadingDeg * 10.0f + 0.5f) % 3600;
  sample.pos_x_ft_x10   = (int16_t)constrain((int)(rxState.posXFt * 10.0f), -32767, 32767);
  sample.pos_y_ft_x10   = (int16_t)constrain((int)(rxState.posYFt * 10.0f), -32767, 32767);
  sample.roll_deg       = (int8_t)constrain((int)roundf(rxState.rollDeg), -127, 127);
  sample.pitch_deg      = (int8_t)constrain((int)roundf(rxState.pitchDeg), -127, 127);
  sample.voltage_dV     = (uint8_t)constrain((int)(rxState.voltageV * 4.0f + 0.5f), 0, 255);
  sample.current_dA     = (uint8_t)constrain((int)(rxState.currentA * 10.0f + 0.5f), 0, 255);

  if (rxState.rxFlashRecords < MAX_RAM_LOG_SAMPLES) {
    rxState.rxFlashRecords++;
  }
}

// Transmit a 4-byte command packet into the Transmitter's 16ms post-TX listening window
inline void sendCommandToTransmitterSlot(uint8_t cmdId) {
  LoRaCommandPacket cmdPkt;
  cmdPkt.magic     = 0xB6;
  cmdPkt.cmd       = cmdId;
  cmdPkt.reset_seq = rxState.resetSeq;
  cmdPkt.checksum  = cmdPkt.magic ^ cmdPkt.cmd ^ cmdPkt.reset_seq;

  delayMicroseconds(1500); // Allow Transmitter to enter radio.startReceive()
  radio.transmit((uint8_t*)&cmdPkt, sizeof(cmdPkt));
  receivedFlag = false;
  radio.startReceive();
}

inline void updateLoRaRate1Hz(unsigned long nowMs) {
  if (nowMs - rxState.lastRateCalcMs >= 1000) {
    rxState.lastRateCalcMs   = nowMs;
    rxState.measuredLoraHz   = rxState.rxPacketsThisSec;
    rxState.rxPacketsThisSec = 0;
  }
}

inline void processIncomingLoRaPackets(unsigned long nowMs) {
  // Check both ISR flag and physical DIO1 pin level so a missed rising edge never locks out RX
  if (!receivedFlag && digitalRead(LORA_DIO1_PIN) == LOW) {
    // Auto-Recovery Watchdog: if no packet has arrived for > 450 ms, re-arm RX; if > 1400 ms, re-init SX1262
    unsigned long silentMs = nowMs - rxState.lastRxTimeMs;
    if (silentMs > 450 && (nowMs - lastWatchdogMs > 450)) {
      lastWatchdogMs = nowMs;
      if (silentMs > 1400) {
        configureLoRaReceiverRadio();
      } else {
        radio.startReceive();
      }
    }
    return;
  }
  receivedFlag   = false;
  lastWatchdogMs = nowMs;

  LoRaTelemetryPacket pkt;
  int state = radio.readData((uint8_t*)&pkt, sizeof(pkt));

  // Immediately re-arm SX1262 receiver
  radio.startReceive();

  if (state == RADIOLIB_ERR_NONE && pkt.magic == 0xA5) {
    unsigned long prevRxMs = rxState.lastRxTimeMs;
    rxState.totalRxPackets++;
    rxState.rxPacketsThisSec++;
    rxState.lastRxTimeMs = nowMs;

    // 1. Sequence Gap & Packet Loss Calculation (< 2.0% Deliverable Target)
    if (rxState.hasFirstPacket) {
      uint16_t gap = (uint16_t)(pkt.seq - rxState.lastSeq);
      if (gap > 1 && gap < 1000) {
        rxState.lostPackets += (gap - 1);
      }
    }
    rxState.hasFirstPacket = true;
    rxState.lastSeq        = pkt.seq;
    rxState.latestSeq      = pkt.seq;

    // 2. Decode Status Bitmask
    rxState.phoneWifiConnected = (pkt.flags & FLAG_PHONE_WIFI_CONN) != 0;
    rxState.zigStreaming       = (pkt.flags & FLAG_ZIG_STREAMING) != 0;
    rxState.hasQuatPose        = (pkt.flags & FLAG_HAS_QUAT) != 0;
    rxState.simSpeedActive     = (pkt.flags & FLAG_SIM_SPEED) != 0;
    rxState.simBatteryActive   = (pkt.flags & FLAG_SIM_BATTERY) != 0;
    rxState.txFlashOk          = (pkt.flags & FLAG_FLASH_OK) != 0;
    rxState.txRaceRunning      = (pkt.flags & FLAG_RACE_RUNNING) != 0;
    rxState.txFlashRecords     = (uint32_t)pkt.tx_records * 10UL; // 60 Hz sample count from TX
    rxState.zigUdpHz           = pkt.zig_hz;
    rxState.phoneWifiRssi      = pkt.phone_rssi;

    // 3. Decode Attitude (Always live even when race timer is PAUSED!)
    rxState.rawHeadingDeg = (pkt.heading_x10 % 3600) * 0.1f;
    float disp = fmodf(rxState.rawHeadingDeg - rxState.zeroOffsetDeg + 360.0f, 360.0f);
    if (disp < 0.0f) disp += 360.0f;
    rxState.displayHeadingDeg = disp;

    rxState.rollDeg  = pkt.roll_x10 * 0.1f;
    rxState.pitchDeg = pkt.pitch_x10 * 0.1f;

    // Calculate elapsed time since previous 10 Hz LoRa packet
    unsigned long dtMs = (prevRxMs > 0 && (nowMs - prevRxMs) < 500) ? (nowMs - prevRxMs) : 100UL;
    float dtSec = dtMs * 0.001f;

    // 4. Decode 60 Hz Transmitter-Computed Position, Distance, Speed & 48V Battery
    float diaScale = rxState.wheelDiameterIn / DEFAULT_WHEEL_DIAMETER_IN;
    float circumferenceFt = (PI * rxState.wheelDiameterIn) / 12.0f;
    float txDistFt = (pkt.distance_ft_x10 * 0.1f) * diaScale;
    float txPosXFt = (pkt.pos_x_ft_x10 * 0.1f) * diaScale;
    float txPosYFt = (pkt.pos_y_ft_x10 * 0.1f) * diaScale;

    if (rxState.desiredRaceRunning) {
      rxState.activeRaceTimeMs += dtMs;

      float txRawMph = pkt.speed_mph_x100 * 0.01f;
      if (txRawMph < 0.05f && rxState.simSpeedActive) {
        float tSec = rxState.activeRaceTimeMs * 0.001f;
        txRawMph = 8.0f - 4.0f * cosf((2.0f * PI * tSec) / 10.0f);
      }

      rxState.speedMph = txRawMph * diaScale;
      rxState.speedFts = rxState.speedMph * 1.4666667f;
      rxState.wheelRpm = (circumferenceFt > 0.01f)
                           ? ((rxState.speedFts * 60.0f) / circumferenceFt)
                           : (pkt.wheel_rpm_x10 * 0.1f);
      rxState.lateralG = pkt.lat_g_x100 * 0.01f;

      // Apply 60 Hz Transmitter-integrated distance & integrate 2D velocity along zeroed displayHeadingDeg
      float dsFt = 0.0f;
      if (!rxState.hasInitialDist || txDistFt < rxState.prevTxDistanceFt) {
        rxState.hasInitialDist = true;
        dsFt = rxState.speedFts * dtSec;
      } else {
        dsFt = txDistFt - rxState.prevTxDistanceFt;
        if (dsFt <= 0.0f || dsFt > 25.0f) {
          dsFt = rxState.speedFts * dtSec;
        }
      }
      rxState.prevTxDistanceFt = txDistFt;

      // Integrate velocity vector strictly along rxState.displayHeadingDeg (the exact headlight direction!)
      float headingRad = rxState.displayHeadingDeg * (PI / 180.0f);
      rxState.totalDistanceFt += dsFt;
      rxState.lapDistanceFt   += dsFt;
      rxState.posXFt          += dsFt * sinf(headingRad);
      rxState.posYFt          += dsFt * cosf(headingRad);
      rxState.uncertaintyFt    = constrain(rxState.uncertaintyFt + (dsFt * 0.008f), 1.5f, 35.0f);

      // 48V Nominal Pack Sinusoidal Current & Active Drain (54.6V -> 42.0V over 300s)
      float txRawAmps = pkt.current_ma_x10 * 0.01f;
      if (txRawAmps < 0.1f && rxState.simBatteryActive) {
        float speedRatio = constrain(rxState.speedMph / 12.0f, 0.0f, 1.0f);
        float rippleA    = 0.35f * sinf((2.0f * PI * (rxState.activeRaceTimeMs * 0.001f)) / 3.3f);
        txRawAmps        = 1.0f + (11.0f * speedRatio) + rippleA;
      }
      rxState.currentA = txRawAmps;

      float elapsedDrainSec = rxState.activeRaceTimeMs * 0.001f;
      float cycleProgress   = fmodf(elapsedDrainSec / 300.0f, 1.0f);
      rxState.socPct        = constrain(100.0f - (cycleProgress * 90.0f), 10.0f, 100.0f);

      float socFrac      = rxState.socPct * 0.01f;
      float openCircuitV = 42.0f + socFrac * (54.6f - 42.0f);
      rxState.voltageV   = constrain(openCircuitV - (rxState.currentA * 0.12f), 42.0f, 54.6f);
      rxState.powerW     = rxState.voltageV * rxState.currentA;
      rxState.energyWh  += (rxState.powerW * dtSec) / 3600.0f;
    } else {
      // PAUSED: Hold speed and load current at 0.0 and track TX baseline so no jump occurs on resume
      rxState.speedMph         = 0.0f;
      rxState.speedFts         = 0.0f;
      rxState.wheelRpm         = 0.0f;
      rxState.lateralG         = 0.0f;
      rxState.currentA         = 0.0f;
      rxState.powerW           = 0.0f;
      rxState.prevTxDistanceFt = txDistFt;
      rxState.prevTxPosXFt     = txPosXFt;
      rxState.prevTxPosYFt     = txPosYFt;
      rxState.hasInitialDist   = true;
      rxState.hasInitialTxPos  = true;
      float socFrac            = rxState.socPct * 0.01f;
      rxState.voltageV         = 42.0f + socFrac * (54.6f - 42.0f);
    }

    // 7. Radio Signal Quality
    rxState.rssi = radio.getRSSI();
    rxState.snr  = radio.getSNR();

    // 8. Non-Blocking USB Serial Output (compatible with Web Serial & Serial Plotter)
    if (Serial && Serial.availableForWrite() >= 192) {
      float rad = rxState.displayHeadingDeg * (PI / 180.0f);
      Serial.printf(
        "Running:%d,RaceMs:%lu,Min:0.0,Heading:%.1f,Max:360.0,SinX:%.2f,CosY:%.2f,RSSI:%.1f,SNR:%.1f,"
        "Roll:%.1f,Pitch:%.1f,LatG:%.2f,Speed:%.2f,RPM:%.1f,DistFt:%.1f,LapFt:%.1f,"
        "PosX:%.1f,PosY:%.1f,ErrFt:%.1f,Volt:%.2f,Curr:%.2f,PowerW:%.1f,EnergyWh:%.1f,SoC:%.1f,"
        "PhoneConn:%d,Streaming:%d,ZigHz:%u,PhoneRSSI:%d,SimSpd:%d,SimBat:%d,"
        "TxRec:%u,Rx:%lu,Lost:%lu\n",
        rxState.desiredRaceRunning ? 1 : 0,
        (unsigned long)rxState.activeRaceTimeMs,
        rxState.displayHeadingDeg, sinf(rad), cosf(rad), rxState.rssi, rxState.snr,
        rxState.rollDeg, rxState.pitchDeg, rxState.lateralG,
        rxState.speedMph, rxState.wheelRpm, rxState.totalDistanceFt, rxState.lapDistanceFt,
        rxState.posXFt, rxState.posYFt, rxState.uncertaintyFt,
        rxState.voltageV, rxState.currentA, rxState.powerW, rxState.energyWh, rxState.socPct,
        rxState.phoneWifiConnected ? 1 : 0,
        rxState.zigStreaming ? 1 : 0,
        (unsigned)rxState.zigUdpHz,
        (int)rxState.phoneWifiRssi,
        rxState.simSpeedActive ? 1 : 0,
        rxState.simBatteryActive ? 1 : 0,
        (unsigned)rxState.txFlashRecords,
        (unsigned long)rxState.totalRxPackets,
        (unsigned long)rxState.lostPackets
      );
    }
  }
}
