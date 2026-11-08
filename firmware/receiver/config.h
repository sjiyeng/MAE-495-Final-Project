/*
  =============================================================================
  config.h — ASME e-HPVC Pitwall Receiver Configuration
  Target Hardware: Seeed Studio XIAO ESP32S3 + Wio-SX1262 (B2B Connector)
  =============================================================================
*/

#pragma once
#include <Arduino.h>

// Wi-Fi Access Point hosted by Pitwall Receiver (http://192.168.4.1)
#define RX_AP_SSID                "ASME-Pitwall-RX"

// Wio-SX1262 B2B LoRa Radio Pins
#define LORA_LDO_EN_PIN           38      // RF Switch / LDO Enable (Active HIGH)
#define LORA_NSS_PIN              41      // SPI Chip Select
#define LORA_DIO1_PIN             39      // Hardware Interrupt (DIO1)
#define LORA_NRST_PIN             42      // Radio Reset
#define LORA_BUSY_PIN             40      // Radio Busy Status

// High-Rate LoRa Parameters (Must match Transmitter config.h)
#define LORA_FREQ_MHZ             915.0f
#define LORA_BW_KHZ               500.0f
#define LORA_SF                   7
#define LORA_CR                   5
#define LORA_PWR_DBM              2
#define LORA_PREAMBLE_LEN         8
#define LORA_TCXO_VOLTS           1.6f

#define DEFAULT_WHEEL_DIAMETER_IN 20.0f

// 60 Hz Local RAM Logger (2.5 Hours = 9,000s * 60 Hz = 540,000 samples * 12B = 6.18 MB in 8 MB OPI PSRAM)
#define RAM_LOG_RATE_HZ           60
#define RAM_LOG_INTERVAL_US       16667UL // 16,667 us = 60.0 Hz sample period
#define MAX_RAM_LOG_SAMPLES       540000UL // 2.5 hours * 3600 s/hr * 60 Hz = 540,000 samples
