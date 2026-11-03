/*
  =============================================================================
  ASME e-HPVC Telemetry System - 60 Hz ZIG SIM (iPhone 17) -> LoRa Transmitter
  Target Hardware: Seeed Studio XIAO ESP32S3 + Wio-SX1262 (B2B Connector)
  
  Arduino IDE Tabs:
    - zig_lora_transmitter.ino : Main setup() and loop() orchestration
    - imu_math.h               : 3D Quaternion reference pose lock & Euler math
    - zig_sim_wifi.h           : SoftAP, iPhone Wi-Fi RSSI, and ZIG SIM UDP parser
    - lora_protocol.h          : Wio-SX1262 60 Hz config, packet struct & DIO1 ISR
  =============================================================================
*/

#include <Arduino.h>
#include "imu_math.h"
#include "zig_sim_wifi.h"
#include "lora_protocol.h"

PhoneTelemetry telemetry;

unsigned long lastTxTime    = 0;
unsigned long lastStatsTime = 0;
const unsigned long TX_INTERVAL_MS = 20; // 50 Hz (20 ms) gives ~14 ms RF turnaround margin
uint16_t packetSeq = 0;

void handleSerialCommands() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'z' || cmd == 'Z') {
      zeroCurrentHeading();
    } else if (cmd == 'c' || cmd == 'C') {
      resetCompassHeading();
    }
  }
}

void updateDiagnostics1Hz(unsigned long now) {
  if (now - lastStatsTime >= 1000) {
    lastStatsTime = now;
    telemetry.measuredZigHz = (telemetry.udpPacketsThisSec > 255) ? 255 : (uint8_t)telemetry.udpPacketsThisSec;
    telemetry.udpPacketsThisSec = 0;
    updatePhoneWifiDiagnostics();
  }
}

void transmitAndPrint(unsigned long now) {
  if (now - lastTxTime < TX_INTERVAL_MS) return;
  lastTxTime = now;

  bool isZigStreaming = (telemetry.lastUdpTimeMs > 0) && (now - telemetry.lastUdpTimeMs <= 500);
  uint8_t statusFlags = 0;
  if (telemetry.phoneWifiConnected) statusFlags |= FLAG_PHONE_WIFI_CONN;
  if (isZigStreaming)               statusFlags |= FLAG_ZIG_STREAMING;
  if (telemetry.hasQuat)            statusFlags |= FLAG_HAS_QUAT;

  float heading = telemetry.quatHeadingDeg;

  // Only increment packetSeq when we ACTUALLY transmit a packet over LoRa!
  if (!radioBusy) {
    LoRaHeadingPacket pkt;
    pkt.magic       = 0xA5;
    pkt.flags       = statusFlags;
    pkt.seq         = packetSeq + 1;
    pkt.heading_x10 = (uint16_t)(heading * 10.0f + 0.5f) % 3600;
    pkt.roll_x10    = (int16_t)(telemetry.rollDeg * 10.0f);
    pkt.pitch_x10   = (int16_t)(telemetry.pitchDeg * 10.0f);
    pkt.zig_hz      = isZigStreaming ? telemetry.measuredZigHz : 0;
    pkt.phone_rssi  = telemetry.phoneWifiRssi;

    if (startNonBlockingLoRaTx(pkt, now)) {
      packetSeq++;
    }
  }

  // Non-blocking USB Serial output (won't stall loop if no Serial Monitor is open)
  if (Serial && Serial.availableForWrite() >= 128) {
    float rad = heading * (PI / 180.0f);
    Serial.printf("Min:0.0,Heading:%.1f,Max:360.0,SinX:%.2f,CosY:%.2f,Roll:%.1f,Pitch:%.1f,PhoneConn:%d,Streaming:%d,ZigHz:%u,PhoneRSSI:%d\n",
                  heading, sinf(rad), cosf(rad), telemetry.rollDeg, telemetry.pitchDeg,
                  telemetry.phoneWifiConnected ? 1 : 0,
                  isZigStreaming ? 1 : 0,
                  isZigStreaming ? telemetry.measuredZigHz : 0,
                  (int)telemetry.phoneWifiRssi);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  initZigSimWifi();
  initLoRaTransmitter();
}

void loop() {
  unsigned long now = millis();

  handleSerialCommands();
  pollZigSimUdp();
  updateDiagnostics1Hz(now);
  pollLoRaTxState(now);
  transmitAndPrint(now);
}
