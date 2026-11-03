/*
  =============================================================================
  lora_protocol.h — 60 Hz Wio-SX1262 LoRa Config, Packet Struct & Non-Blocking TX
  =============================================================================
*/

#pragma once
#include <Arduino.h>
#include <RadioLib.h>

// Pin Definitions for Seeed XIAO ESP32S3 + Wio-SX1262 B2B Connector
#define LORA_LDO_EN_PIN   38   // Power / RF switch enable on Wio-SX1262
#define LORA_NSS_PIN      41   // SPI Chip Select
#define LORA_DIO1_PIN     39   // Interrupt (DIO1)
#define LORA_NRST_PIN     42   // Hardware Reset
#define LORA_BUSY_PIN     40   // Busy status line

// RF Configuration Tuned for High-Rate Ultra-Low Airtime (~5.3 ms per packet)
const float    LORA_FREQ_MHZ = 915.0;
const float    LORA_BW_KHZ   = 500.0;
const uint8_t  LORA_SF       = 7;
const uint8_t  LORA_CR       = 5;
const uint8_t  LORA_SYNC     = RADIOLIB_SX126X_SYNC_WORD_PRIVATE;
const int8_t   LORA_PWR      = 10;     // 10 dBm prevents RX LNA saturation on bench while covering full track
const uint16_t LORA_PREAMBLE = 8;      // Standard 8-symbol preamble for reliable SF7 sync
const float    LORA_TCXO     = 1.6;

// Status Bitmask Flags
#define FLAG_PHONE_WIFI_CONN  0x01  // iPhone is connected to "ASME-Trike-TX" Wi-Fi
#define FLAG_ZIG_STREAMING    0x02  // ZIG SIM UDP packets arriving (< 500 ms ago)
#define FLAG_HAS_QUAT         0x04  // Valid 3D Quaternion reference pose locked

// Compact 12-Byte Binary LoRa Packet (Must match Receiver)
struct __attribute__((packed)) LoRaHeadingPacket {
  uint8_t  magic;         // 0xA5 packet identifier
  uint8_t  flags;         // Status bitmask (Phone Wi-Fi, ZIG Streaming, Has Quat)
  uint16_t seq;           // Rolling packet sequence counter
  uint16_t heading_x10;   // Heading * 10 (0 .. 3600 -> 0.0 .. 360.0 deg)
  int16_t  roll_x10;      // Roll * 10 (-1800 .. +1800 -> -180.0 .. +180.0 deg)
  int16_t  pitch_x10;     // Pitch * 10 (-900 .. +900 -> -90.0 .. +90.0 deg)
  uint8_t  zig_hz;        // Measured ZIG SIM UDP packet rate on Transmitter (Hz)
  int8_t   phone_rssi;    // iPhone Wi-Fi RSSI at Transmitter (dBm)
};

static SX1262 radio = new Module(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_NRST_PIN, LORA_BUSY_PIN);
static volatile bool txDoneFlag = false;
static bool radioBusy = false;
static unsigned long txStartTimeMs = 0;

#if defined(ESP32) || defined(ESP8266)
IRAM_ATTR
#endif
void onTxDone(void) {
  txDoneFlag = true;
}

inline void initLoRaTransmitter() {
  pinMode(LORA_LDO_EN_PIN, OUTPUT);
  digitalWrite(LORA_LDO_EN_PIN, HIGH);
  delay(50);

  radio.tcxoVoltage = LORA_TCXO;
  int state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC, LORA_PWR, LORA_PREAMBLE);
  if (state != RADIOLIB_ERR_NONE) {
    while (true) {
      Serial.printf("LoRa_Init_Failed:%d\n", state);
      delay(1000);
    }
  }
  radio.setDio1Action(onTxDone);
}

inline void pollLoRaTxState(unsigned long now) {
  if (txDoneFlag) {
    txDoneFlag = false;
    radio.finishTransmit();
    radioBusy = false;
  } else if (radioBusy && (now - txStartTimeMs > 50)) {
    radio.finishTransmit();
    radioBusy = false;
  }
}

inline bool startNonBlockingLoRaTx(const LoRaHeadingPacket& pkt, unsigned long now) {
  if (radioBusy) return false;
  int state = radio.startTransmit((uint8_t*)&pkt, sizeof(pkt));
  if (state == RADIOLIB_ERR_NONE) {
    radioBusy = true;
    txStartTimeMs = now;
    return true;
  }
  return false;
}
