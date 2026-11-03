/*
  =============================================================================
  ASME e-HPVC Telemetry System - 60 Hz LoRa Receiver + Wi-Fi & Serial Dashboard
  Target Hardware: Seeed Studio XIAO ESP32S3 + Wio-SX1262 (B2B Connector)
  
  Arduino IDE Tabs:
    - zig_lora_receiver.ino : Main setup() and loop() orchestration
    - lora_protocol.h       : Wio-SX1262 60 Hz config, packet struct & DIO1 ISR
    - web_server.h          : "ASME-Pitwall-RX" Wi-Fi SoftAP & HTTP JSON endpoints
    - dashboard_html.h      : 2-Tab PROGMEM HTML Webpage (Compass + Diagnostics)
  =============================================================================
*/

#include <Arduino.h>
#include "lora_protocol.h"
#include "web_server.h"

ReceiverState rxState;

void handleSerialCommands() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'z' || cmd == 'Z') {
      rxState.zeroOffsetDeg = rxState.rawHeadingDeg;
    } else if (cmd == 'c' || cmd == 'C') {
      rxState.zeroOffsetDeg = 0.0f;
    } else if (cmd == 'r' || cmd == 'R') {
      resetPacketLossStats();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  initLoRaReceiver();
  initDashboardServer();
}

void loop() {
  unsigned long now = millis();

  updateLoRaRate1Hz(now);
  handleSerialCommands();
  processIncomingLoRaPackets(now);
  pollDashboardServer();
}
