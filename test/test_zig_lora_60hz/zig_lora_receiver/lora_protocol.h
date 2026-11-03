/*
  =============================================================================
  lora_protocol.h — 60 Hz Wio-SX1262 LoRa Config, Packet Struct & Receiver ISR
  =============================================================================
*/

#pragma once
#include <Arduino.h>
#include <RadioLib.h>
#include <math.h>

// Pin Definitions for Seeed XIAO ESP32S3 + Wio-SX1262 B2B Connector
#define LORA_LDO_EN_PIN   38   // Power / RF switch enable on Wio-SX1262
#define LORA_NSS_PIN      41   // SPI Chip Select
#define LORA_DIO1_PIN     39   // Interrupt (DIO1)
#define LORA_NRST_PIN     42   // Hardware Reset
#define LORA_BUSY_PIN     40   // Busy status line

// RF Configuration (Must match Transmitter settings)
const float    LORA_FREQ_MHZ = 915.0;
const float    LORA_BW_KHZ   = 500.0;
const uint8_t  LORA_SF       = 7;
const uint8_t  LORA_CR       = 5;
const uint8_t  LORA_SYNC     = RADIOLIB_SX126X_SYNC_WORD_PRIVATE;
const int8_t   LORA_PWR      = 10;
const uint16_t LORA_PREAMBLE = 8;
const float    LORA_TCXO     = 1.6;

// Status Bitmask Flags
#define FLAG_PHONE_WIFI_CONN  0x01  // iPhone is connected to "ASME-Trike-TX" Wi-Fi
#define FLAG_ZIG_STREAMING    0x02  // ZIG SIM UDP packets arriving (< 500 ms ago)
#define FLAG_HAS_QUAT         0x04  // Valid 3D Quaternion reference pose locked

// Compact 12-Byte Binary LoRa Packet (Must match Transmitter)
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

// Receiver State & Diagnostic Statistics
struct ReceiverState {
  float rawHeadingDeg     = 0.0f;
  float displayHeadingDeg = 0.0f;
  float zeroOffsetDeg     = 0.0f;
  float rollDeg           = 0.0f;
  float pitchDeg          = 0.0f;
  float rssi              = -127.0f;
  float snr               = 0.0f;

  bool     phoneWifiConnected = false;
  bool     zigStreaming       = false;
  bool     hasQuatPose        = false;
  uint8_t  zigUdpHz           = 0;
  int8_t   phoneWifiRssi      = -127;

  uint16_t latestSeq          = 0;
  uint16_t lastSeq            = 0;
  bool     hasFirstPacket     = false;
  uint32_t totalRxPackets     = 0;
  uint32_t lostPackets        = 0;
  unsigned long lastRxTimeMs  = 0;

  uint16_t rxPacketsThisSec   = 0;
  uint16_t measuredLoraHz     = 0;
  unsigned long lastRateCalcMs = 0;
};

extern ReceiverState rxState;

static SX1262 radio = new Module(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_NRST_PIN, LORA_BUSY_PIN);
static volatile bool receivedFlag = false;

#if defined(ESP32) || defined(ESP8266)
IRAM_ATTR
#endif
void onPacketReceived(void) {
  receivedFlag = true;
}

inline void initLoRaReceiver() {
  pinMode(LORA_LDO_EN_PIN, OUTPUT);
  digitalWrite(LORA_LDO_EN_PIN, HIGH);
  delay(50);

  radio.tcxoVoltage = LORA_TCXO;
  int state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC, LORA_PWR, LORA_PREAMBLE);
  if (state != RADIOLIB_ERR_NONE) {
    while (true) {
      Serial.printf("LoRa_RX_Init_Failed:%d\n", state);
      delay(1000);
    }
  }

  radio.setDio1Action(onPacketReceived);
  radio.startReceive();
}

inline void updateLoRaRate1Hz(unsigned long now) {
  if (now - rxState.lastRateCalcMs >= 1000) {
    rxState.lastRateCalcMs = now;
    rxState.measuredLoraHz = rxState.rxPacketsThisSec;
    rxState.rxPacketsThisSec = 0;
  }
}

inline void resetPacketLossStats() {
  rxState.totalRxPackets = 0;
  rxState.lostPackets    = 0;
  rxState.hasFirstPacket = false;
}

inline void processIncomingLoRaPackets(unsigned long now) {
  if (!receivedFlag) return;
  receivedFlag = false;

  LoRaHeadingPacket pkt;
  int state = radio.readData((uint8_t*)&pkt, sizeof(pkt));

  // Immediately re-arm receiver for the next 60 Hz packet
  radio.startReceive();

  if (state == RADIOLIB_ERR_NONE && pkt.magic == 0xA5) {
    rxState.totalRxPackets++;
    rxState.rxPacketsThisSec++;
    rxState.lastRxTimeMs = now;

    if (rxState.hasFirstPacket) {
      uint16_t gap = (uint16_t)(pkt.seq - rxState.lastSeq);
      if (gap > 1 && gap < 1000) {
        rxState.lostPackets += (gap - 1);
      }
    }
    rxState.hasFirstPacket = true;
    rxState.lastSeq   = pkt.seq;
    rxState.latestSeq = pkt.seq;

    rxState.phoneWifiConnected = (pkt.flags & FLAG_PHONE_WIFI_CONN) != 0;
    rxState.zigStreaming       = (pkt.flags & FLAG_ZIG_STREAMING) != 0;
    rxState.hasQuatPose        = (pkt.flags & FLAG_HAS_QUAT) != 0;
    rxState.zigUdpHz           = pkt.zig_hz;
    rxState.phoneWifiRssi      = pkt.phone_rssi;

    rxState.rawHeadingDeg = (pkt.heading_x10 % 3600) * 0.1f;
    float disp = fmodf(rxState.rawHeadingDeg - rxState.zeroOffsetDeg + 360.0f, 360.0f);
    if (disp < 0.0f) disp += 360.0f;
    rxState.displayHeadingDeg = disp;

    rxState.rollDeg  = pkt.roll_x10 * 0.1f;
    rxState.pitchDeg = pkt.pitch_x10 * 0.1f;
    rxState.rssi     = radio.getRSSI();
    rxState.snr      = radio.getSNR();

    // Non-blocking USB Serial output (never stalls LoRa RX when Serial Monitor is closed)
    if (Serial && Serial.availableForWrite() >= 128) {
      float rad = rxState.displayHeadingDeg * (PI / 180.0f);
      Serial.printf("Min:0.0,Heading:%.1f,Max:360.0,SinX:%.2f,CosY:%.2f,RSSI:%.1f,SNR:%.1f,Roll:%.1f,Pitch:%.1f,PhoneConn:%d,Streaming:%d,ZigHz:%u,PhoneRSSI:%d,Rx:%lu,Lost:%lu\n",
                    rxState.displayHeadingDeg, sinf(rad), cosf(rad), rxState.rssi, rxState.snr,
                    rxState.rollDeg, rxState.pitchDeg,
                    rxState.phoneWifiConnected ? 1 : 0,
                    rxState.zigStreaming ? 1 : 0,
                    (unsigned)rxState.zigUdpHz,
                    (int)rxState.phoneWifiRssi,
                    (unsigned long)rxState.totalRxPackets,
                    (unsigned long)rxState.lostPackets);
    }
  }
}
