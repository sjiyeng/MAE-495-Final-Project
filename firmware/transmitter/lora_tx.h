/*
  =============================================================================
  lora_tx.h — Wio-SX1262 Non-Blocking 40 Hz Telemetry Transmitter + Command RX
  Listens during the inter-packet window (~16 ms) for 4-byte Start/Pause/Reset
  commands from the Pitwall Receiver.
  =============================================================================
*/

#pragma once
#include <Arduino.h>
#include <RadioLib.h>
#include "config.h"
#include "telemetry_types.h"
#include "wheel_sensor.h"
#include "power_ina228.h"
#include "kalman_nav.h"

extern TrikeTelemetryState trikeState;

struct LowSpikeSX1262 : public SX1262 {
  using SX1262::SX1262;
  int16_t configureLowCurrentSpikes(int8_t powerDbm) {
    // 1. Force high-efficiency internal DC-DC buck converter (~50% lower current than LDO)
    int16_t state = setRegulatorDCDC();
    // 2. Slow PA ramp-up time to 800 us (vs 40 us default) to eliminate steep di/dt rail dips
    int8_t rawPwr = constrain((int)powerDbm, -9, 22);
    state |= SX126x::setTxParams(rawPwr, RADIOLIB_SX126X_PA_RAMP_800U);
    return state;
  }
};

static LowSpikeSX1262 radio = new Module(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_NRST_PIN, LORA_BUSY_PIN);
static volatile bool dio1IrqFlag    = false;
static bool radioTransmitting       = false;
static unsigned long txStartTimeMs  = 0;
static uint16_t packetSeq           = 0;
static uint8_t consecutiveTxErrors  = 0;

#if defined(ESP32)
IRAM_ATTR
#endif
void onLoRaDio1Irq(void) {
  dio1IrqFlag = true;
}

inline void resetEntireRaceStateOnTransmitter() {
  trikeState.activeSimTimeMs  = 0;
  trikeState.txRamSamples60Hz = 0;
  resetWheelSensorState();
  resetBatterySimulation();
  snapNavToStartPin();
}

inline bool configureLoRaTransmitterRadio() {
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
    radio.setDio1Action(onLoRaDio1Irq);
    radio.standby();
    radioTransmitting   = false;
    dio1IrqFlag         = false;
    consecutiveTxErrors = 0;
    return true;
  }
  return false;
}

inline void initLoRaTransmitter() {
  pinMode(LORA_LDO_EN_PIN, OUTPUT);
  digitalWrite(LORA_LDO_EN_PIN, HIGH);
  pinMode(LORA_DIO1_PIN, INPUT);
  delay(50);

  if (!configureLoRaTransmitterRadio()) {
    while (true) {
      Serial.println("LoRa_TX_Init_Failed");
      delay(1000);
    }
  }
}

inline void pollLoRaTxState(unsigned long nowMs) {
  // Check both the ISR flag and physical DIO1 pin level so a missed edge can never stall TX
  if (dio1IrqFlag || (radioTransmitting && digitalRead(LORA_DIO1_PIN) == HIGH)) {
    dio1IrqFlag = false;
    if (radioTransmitting) {
      radio.finishTransmit();
      radio.standby(); // Stay in clean standby between 10 Hz transmissions
      radioTransmitting = false;
    }
  } else if (radioTransmitting && (nowMs - txStartTimeMs > 65)) {
    // Hardware TX timeout recovery: finish transmit and re-verify radio if needed
    radio.finishTransmit();
    radio.standby();
    radioTransmitting = false;
    consecutiveTxErrors++;
    if (consecutiveTxErrors >= 3) {
      configureLoRaTransmitterRadio();
    }
  }
}

inline bool transmitTelemetryPacket(unsigned long nowMs) {
  if (radioTransmitting) return false;

  // Hold ZIG streaming status green unless UDP packets have stopped for > 3.0 seconds
  bool isZigStreaming = (trikeState.lastUdpTimeMs > 0) && (nowMs - trikeState.lastUdpTimeMs <= 3000);
  uint8_t statusFlags = 0;
  if (trikeState.phoneWifiConnected) statusFlags |= FLAG_PHONE_WIFI_CONN;
  if (isZigStreaming)                statusFlags |= FLAG_ZIG_STREAMING;
  if (trikeState.hasQuat)            statusFlags |= FLAG_HAS_QUAT;
  if (trikeState.simWheelSpeed)      statusFlags |= FLAG_SIM_SPEED;
  if (trikeState.simBattery)         statusFlags |= FLAG_SIM_BATTERY;
  if (trikeState.ramLogReady || trikeState.flashReady) statusFlags |= FLAG_FLASH_OK;
  if (trikeState.raceRunning)        statusFlags |= FLAG_RACE_RUNNING;

  LoRaTelemetryPacket pkt;
  pkt.magic           = 0xA5;
  pkt.flags           = statusFlags;
  pkt.seq             = packetSeq + 1;
  pkt.uptime_ms       = trikeState.activeSimTimeMs;

  pkt.heading_x10     = (uint16_t)(trikeState.headingDeg * 10.0f + 0.5f) % 3600;
  pkt.roll_x10        = (int16_t)(trikeState.rollDeg * 10.0f);
  pkt.pitch_x10       = (int16_t)(trikeState.pitchDeg * 10.0f);
  pkt.lat_g_x100      = (int16_t)(trikeState.lateralG * 100.0f);

  pkt.speed_mph_x100  = (uint16_t)(constrain(trikeState.speedMph, 0.0f, 99.9f) * 100.0f + 0.5f);
  pkt.wheel_rpm_x10   = (uint16_t)(constrain(trikeState.wheelRpm, 0.0f, 6000.0f) * 10.0f + 0.5f);
  pkt.distance_ft_x10 = (uint32_t)(max(0.0f, trikeState.totalDistanceFt) * 10.0f + 0.5f);
  pkt.pos_x_ft_x10    = (int32_t)roundf(trikeState.posXFt * 10.0f);
  pkt.pos_y_ft_x10    = (int32_t)roundf(trikeState.posYFt * 10.0f);

  pkt.voltage_mv      = (uint16_t)constrain((int)(trikeState.voltageV * 1000.0f + 0.5f), 0, 65535);
  pkt.current_ma_x10  = (int16_t)constrain((int)(trikeState.currentA * 100.0f + 0.5f), -32000, 32000);
  pkt.energy_wh_x10   = (uint16_t)constrain((int)(trikeState.energyWh * 10.0f + 0.5f), 0, 65535);
  pkt.soc_x10         = (uint16_t)constrain((int)(trikeState.socPct * 10.0f + 0.5f), 0, 1000);

  // Send 60 Hz RAM sample count in units of 10 samples (0..54,000 = 0..540,000 samples for 2.5 hrs @ 60 Hz)
  pkt.tx_records      = (uint16_t)min(65535UL, (unsigned long)(trikeState.txRamSamples60Hz / 10UL));
  pkt.zig_hz          = isZigStreaming ? trikeState.measuredZigHz : 0;
  pkt.phone_rssi      = trikeState.phoneWifiRssi;

  int state = radio.startTransmit((uint8_t*)&pkt, sizeof(pkt));
  if (state == RADIOLIB_ERR_NONE) {
    radioTransmitting   = true;
    txStartTimeMs       = nowMs;
    consecutiveTxErrors = 0;
    packetSeq++;
    return true;
  } else {
    // Auto-recover SX1262 if SPI or BUSY glitched due to breadboard supply dip
    consecutiveTxErrors++;
    radio.standby();
    if (consecutiveTxErrors >= 3) {
      configureLoRaTransmitterRadio();
    }
    return false;
  }
}
