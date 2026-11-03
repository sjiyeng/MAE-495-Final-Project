/*
  =============================================================================
  zig_sim_wifi.h — Wi-Fi SoftAP, iPhone Station RSSI, and ZIG SIM UDP Parser
  =============================================================================
*/

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>
#include "imu_math.h"

const char* AP_SSID     = "ASME-Trike-TX"; // Open Wi-Fi network
const uint16_t UDP_PORT = 50000;           // Default port in ZIG SIM app

static WiFiUDP udp;
static char udpBuffer[2048];

inline void initZigSimWifi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  WiFi.setSleep(false);
  delay(100);
  udp.begin(UDP_PORT);
}

inline void updatePhoneWifiDiagnostics() {
  wifi_sta_list_t staList;
  if (esp_wifi_ap_get_sta_list(&staList) == ESP_OK && staList.num > 0) {
    telemetry.phoneWifiConnected = true;
    telemetry.phoneWifiRssi      = staList.sta[0].rssi;
  } else {
    telemetry.phoneWifiConnected = false;
    telemetry.phoneWifiRssi      = -127;
  }
}

inline bool extractJsonFloat(const char* sectionStart, const char* sectionEnd, const char* key, float& outVal) {
  const char* p = strstr(sectionStart, key);
  if (!p || p >= sectionEnd) return false;
  p += strlen(key);
  while (p < sectionEnd && (*p == ' ' || *p == '\t' || *p == ':')) p++;
  if (p >= sectionEnd) return false;
  outVal = strtof(p, nullptr);
  return true;
}

inline float readFloatBE(const uint8_t* ptr) {
  uint32_t raw = ((uint32_t)ptr[0] << 24) |
                 ((uint32_t)ptr[1] << 16) |
                 ((uint32_t)ptr[2] << 8)  |
                 ((uint32_t)ptr[3]);
  float val;
  memcpy(&val, &raw, sizeof(float));
  return val;
}

inline void parseZigSimPacket(const char* buf, int len) {
  // 1. JSON Format
  const char* quatSection = strstr(buf, "\"quaternion\"");
  if (quatSection) {
    const char* closeBrace = strchr(quatSection, '}');
    if (closeBrace) {
      float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
      if (extractJsonFloat(quatSection, closeBrace, "\"x\"", x) &&
          extractJsonFloat(quatSection, closeBrace, "\"y\"", y) &&
          extractJsonFloat(quatSection, closeBrace, "\"z\"", z) &&
          extractJsonFloat(quatSection, closeBrace, "\"w\"", w)) {
        updateQuaternionHeading(x, y, z, w);
        return;
      }
    }
  }

  // 2. OSC Binary Format Fallback
  for (int i = 0; i < len - 28; i++) {
    if (memcmp(&buf[i], "quaternion", 10) == 0) {
      for (int j = i + 10; j < i + 24 && j + 24 <= len; j++) {
        if (memcmp(&buf[j], ",ffff", 5) == 0) {
          const uint8_t* fPtr = (const uint8_t*)&buf[j + 8];
          float x = readFloatBE(fPtr + 0);
          float y = readFloatBE(fPtr + 4);
          float z = readFloatBE(fPtr + 8);
          float w = readFloatBE(fPtr + 12);
          updateQuaternionHeading(x, y, z, w);
          return;
        }
      }
    }
  }
}

inline void pollZigSimUdp() {
  int packetSize = udp.parsePacket();
  while (packetSize > 0) {
    int len = udp.read(udpBuffer, sizeof(udpBuffer) - 1);
    if (len > 0) {
      udpBuffer[len] = '\0';
      parseZigSimPacket(udpBuffer, len);
    }
    packetSize = udp.parsePacket();
  }
}
