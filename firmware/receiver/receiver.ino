/*
  =============================================================================
  ASME e-HPVC Telemetry & Race Management System — Pitwall Receiver Firmware
  Target Hardware: Seeed Studio XIAO ESP32S3 + Wio-SX1262 (B2B Connector)

  Modular Architecture (Arduino IDE Tabs):
    - receiver.ino      : Core 1 LoRa loop & USB Serial command handler
    - config.h          : Receiver hardware pins, SoftAP SSID & RF config
    - telemetry_types.h : Shared binary packet & ReceiverState definitions
    - lora_rx.h         : Core 1 DIO1 interrupt handler, dead-reckoning & stats
    - web_server.h      : Core 0 "ASME-Pitwall-RX" Wi-Fi SoftAP & HTTP API
    - dashboard_html.h  : Offline-ready PROGMEM Pitwall Web Dashboard
  =============================================================================
*/

#include <Arduino.h>
#include "config.h"
#include "telemetry_types.h"
#include "lora_rx.h"
#include "web_server.h"

ReceiverState rxState;

void handleSerialCommands() {
  while (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'z' || cmd == 'Z') {
      rxState.zeroOffsetDeg = rxState.rawHeadingDeg;
    } else if (cmd == 'c' || cmd == 'C') {
      rxState.zeroOffsetDeg = 0.0f;
    } else if (cmd == 'p' || cmd == 'P') {
      setRaceRunningState(true);
    } else if (cmd == 'q' || cmd == 'Q') {
      setRaceRunningState(false);
    } else if (cmd == 't' || cmd == 'T') {
      triggerFullRaceReset();
    } else if (cmd == 'l' || cmd == 'L') {
      snapReceiverPositionToStartPin();
    } else if (cmd == 'r' || cmd == 'R') {
      resetPacketLossStats();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);

  initLoRaReceiver();
  initDashboardServer();
}

void loop() {
  unsigned long nowMs = millis();
  unsigned long nowUs = micros();

  updateLoRaRate1Hz(nowMs);
  handleSerialCommands();
  processIncomingLoRaPackets(nowMs);
  updateReceiverRamLog60Hz(nowUs);
}
