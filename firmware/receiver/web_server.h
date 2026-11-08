/*
  =============================================================================
  web_server.h — Core 0 Wi-Fi SoftAP ("ASME-Pitwall-RX") & JSON REST Endpoints
  Runs pinned to Core 0 so Wi-Fi TCP handshakes never block Core 1's LoRa ISR.
  =============================================================================
*/

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "config.h"
#include "telemetry_types.h"
#include "lora_rx.h"
#include "dashboard_html.h"

static WebServer server(80);

inline void initDashboardServer() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(RX_AP_SSID);
  WiFi.setSleep(false); // Low-latency SoftAP HTTP responses (~2 ms)

  // 1. Main Pitwall Web Dashboard (Offline-Ready Single Page App)
  server.on("/", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send_P(200, "text/html", INDEX_HTML);
  });

  // 2. High-Rate Live Telemetry JSON Endpoint (Polled at 25-30 Hz)
  server.on("/state", []() {
    if (server.hasArg("run")) {
      setRaceRunningState(server.arg("run") == "1");
    }
    if (server.hasArg("zseq")) {
      uint8_t zseq = (uint8_t)server.arg("zseq").toInt();
      if (zseq != rxState.lastWebZeroSeq) {
        rxState.lastWebZeroSeq = zseq;
        rxState.zeroOffsetDeg  = rxState.rawHeadingDeg;
        float disp = fmodf(rxState.rawHeadingDeg - rxState.zeroOffsetDeg + 360.0f, 360.0f);
        if (disp < 0.0f) disp += 360.0f;
        rxState.displayHeadingDeg = disp;
      }
    }
    if (server.hasArg("rseq")) {
      uint8_t rseq = (uint8_t)server.arg("rseq").toInt();
      if (rseq != rxState.lastWebResetSeq) {
        rxState.lastWebResetSeq = rseq;
        triggerFullRaceReset();
      }
    }
    if (server.hasArg("lseq")) {
      uint8_t lseq = (uint8_t)server.arg("lseq").toInt();
      if (lseq != rxState.lastWebLapSeq) {
        rxState.lastWebLapSeq = lseq;
        snapReceiverPositionToStartPin();
      }
    }
    unsigned long ageMs = (rxState.totalRxPackets > 0) ? (millis() - rxState.lastRxTimeMs) : 99999UL;
    uint32_t logged60Hz = max(rxState.rxFlashRecords, rxState.txFlashRecords);
    uint32_t freePsramKb = (uint32_t)(ESP.getFreePsram() / 1024UL);
    uint32_t freeSramKb  = (uint32_t)(ESP.getFreeHeap() / 1024UL);
    char json[640];
    snprintf(
      json, sizeof(json),
      "{\"run\":%d,\"rms\":%lu,\"h\":%.1f,\"r\":%.1f,\"p\":%.1f,\"g\":%.2f,"
      "\"spd\":%.2f,\"fts\":%.2f,\"rpm\":%.1f,\"dist\":%.1f,\"ldist\":%.1f,"
      "\"x\":%.1f,\"y\":%.1f,\"err\":%.1f,"
      "\"v\":%.2f,\"a\":%.2f,\"w\":%.1f,\"wh\":%.1f,\"soc\":%.1f,"
      "\"pc\":%d,\"zs\":%d,\"hq\":%d,\"ss\":%d,\"sb\":%d,\"fo\":%d,"
      "\"trec\":%lu,\"rcap\":%lu,\"pram\":%lu,\"sram\":%lu,\"zhz\":%u,\"prssi\":%d,"
      "\"rssi\":%.1f,\"snr\":%.1f,\"lhz\":%u,"
      "\"seq\":%u,\"rx\":%lu,\"lost\":%lu,\"age\":%lu,\"wdia\":%.1f}",
      rxState.desiredRaceRunning ? 1 : 0,
      (unsigned long)rxState.activeRaceTimeMs,
      rxState.displayHeadingDeg, rxState.rollDeg, rxState.pitchDeg, rxState.lateralG,
      rxState.speedMph, rxState.speedFts, rxState.wheelRpm, rxState.totalDistanceFt, rxState.lapDistanceFt,
      rxState.posXFt, rxState.posYFt, rxState.uncertaintyFt,
      rxState.voltageV, rxState.currentA, rxState.powerW, rxState.energyWh, rxState.socPct,
      rxState.phoneWifiConnected ? 1 : 0,
      rxState.zigStreaming ? 1 : 0,
      rxState.hasQuatPose ? 1 : 0,
      rxState.simSpeedActive ? 1 : 0,
      rxState.simBatteryActive ? 1 : 0,
      rxState.rxRamLogReady ? 1 : (rxState.txFlashOk ? 1 : 0),
      (unsigned long)logged60Hz,
      (unsigned long)rxState.rxRamCapacity,
      (unsigned long)freePsramKb,
      (unsigned long)freeSramKb,
      (unsigned)rxState.zigUdpHz,
      (int)rxState.phoneWifiRssi,
      rxState.rssi, rxState.snr,
      (unsigned)rxState.measuredLoraHz,
      (unsigned)rxState.latestSeq,
      (unsigned long)rxState.totalRxPackets,
      (unsigned long)rxState.lostPackets,
      ageMs,
      rxState.wheelDiameterIn
    );
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  // 3. Start / Pause / Reset Race Timer & Wheel Speed (/timer?run=1 | run=0 | reset=1)
  server.on("/timer", []() {
    if (server.hasArg("reset") && server.arg("reset") == "1") {
      triggerFullRaceReset();
    } else if (server.hasArg("run")) {
      bool run = (server.arg("run") == "1");
      setRaceRunningState(run);
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "OK");
  });

  // 4. Zero Display Heading ('z')
  server.on("/zero", []() {
    rxState.zeroOffsetDeg = rxState.rawHeadingDeg;
    rxState.displayHeadingDeg = 0.0f;
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "OK");
  });

  // 5. Reset Compass Heading ('c')
  server.on("/reset", []() {
    rxState.zeroOffsetDeg = 0.0f;
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "OK");
  });

  // 6. Loop-Closure Lap Snap to Start Pin (0,0) ('l')
  server.on("/lap", []() {
    snapReceiverPositionToStartPin();
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "OK");
  });

  // 7. Clear Packet Loss Counters ('r')
  server.on("/clear_stats", []() {
    resetPacketLossStats();
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "OK");
  });

  // 8. Update 50-Ft Wheel Calibration Diameter (/config?wheel_dia=20.0)
  server.on("/config", []() {
    if (server.hasArg("wheel_dia")) {
      float val = server.arg("wheel_dia").toFloat();
      if (val >= 5.0f && val <= 40.0f) {
        rxState.wheelDiameterIn = val;
      }
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "OK");
  });

  server.begin();

  // Pin WebServer task to Core 0 so HTTP traffic never delays Core 1 LoRa interrupts
  xTaskCreatePinnedToCore(
    [](void* param) {
      for (;;) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(2));
      }
    },
    "PitwallWebServerTask",
    6144,
    nullptr,
    1,
    nullptr,
    0 // Core 0
  );
}
