/*
  =============================================================================
  web_server.h — Receiver Wi-Fi SoftAP ("ASME-Pitwall-RX") & HTTP Routes
  =============================================================================
*/

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "lora_protocol.h"
#include "dashboard_html.h"

const char* RX_AP_SSID = "ASME-Pitwall-RX";
static WebServer server(80);

inline void initDashboardServer() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(RX_AP_SSID);
  WiFi.setSleep(false); // Low-latency SoftAP HTTP responses (~2 ms)

  server.on("/", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/state", []() {
    unsigned long ageMs = (rxState.totalRxPackets > 0) ? (millis() - rxState.lastRxTimeMs) : 99999UL;
    char json[256];
    snprintf(json, sizeof(json),
             "{\"h\":%.1f,\"r\":%.1f,\"p\":%.1f,\"pc\":%d,\"zs\":%d,\"hq\":%d,"
             "\"zhz\":%u,\"prssi\":%d,\"rssi\":%.1f,\"snr\":%.1f,\"lhz\":%u,"
             "\"seq\":%u,\"rx\":%lu,\"lost\":%lu,\"age\":%lu}",
             rxState.displayHeadingDeg, rxState.rollDeg, rxState.pitchDeg,
             rxState.phoneWifiConnected ? 1 : 0,
             rxState.zigStreaming ? 1 : 0,
             rxState.hasQuatPose ? 1 : 0,
             (unsigned)rxState.zigUdpHz,
             (int)rxState.phoneWifiRssi,
             rxState.rssi, rxState.snr,
             (unsigned)rxState.measuredLoraHz,
             (unsigned)rxState.latestSeq,
             (unsigned long)rxState.totalRxPackets,
             (unsigned long)rxState.lostPackets,
             ageMs);
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  server.on("/zero", []() {
    rxState.zeroOffsetDeg = rxState.rawHeadingDeg;
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "OK");
  });

  server.on("/reset", []() {
    rxState.zeroOffsetDeg = 0.0f;
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "OK");
  });

  server.on("/clear_stats", []() {
    resetPacketLossStats();
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "OK");
  });

  server.begin();

  // Run WebServer on Core 0 so HTTP TCP handshakes never block Core 1's LoRa receiver!
  xTaskCreatePinnedToCore(
    [](void* param) {
      for (;;) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(2));
      }
    },
    "WebServerTask",
    4096,
    nullptr,
    1,
    nullptr,
    0 // Core 0
  );
}

inline void pollDashboardServer() {
  // Handled asynchronously on Core 0
}
