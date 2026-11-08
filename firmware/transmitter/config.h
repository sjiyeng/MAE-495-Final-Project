/*
  =============================================================================
  config.h — ASME e-HPVC Transmitter Configuration & Hardware Abstraction Flags
  Target Hardware: Seeed Studio XIAO ESP32S3 + Wio-SX1262 (B2B Connector)
  =============================================================================
*/

#pragma once
#include <Arduino.h>

// -----------------------------------------------------------------------------
// 1. Modular Sensor Simulation Switches
//    Set to `false` when the physical Hall sensor or INA228 is wired up!
//    (Can also be toggled at runtime via Serial commands 's' and 'b')
// -----------------------------------------------------------------------------
#define DEFAULT_SIM_WHEEL_SPEED   true    // true = 10s Sinusoidal 0-10 mph generator
#define DEFAULT_SIM_INA228_POWER  true    // true = 48V pack (54.6V->42.0V) 5-min drain + 10s load sag

// -----------------------------------------------------------------------------
// 2. Sinusoidal Simulation Parameters
// -----------------------------------------------------------------------------
#define SIM_SPEED_PERIOD_SEC      10.0f   // 10-second full wave cycle (4 -> 12 -> 4 mph)
#define SIM_SPEED_MIN_MPH         4.0f    // Minimum simulated wheel speed when running (mph)
#define SIM_SPEED_MAX_MPH         12.0f   // Peak simulated wheel speed when running (mph)

// 48V Nominal Li-ion Pack (13S: 54.6V Full -> 42.0V Empty)
#define SIM_BATT_FULL_MV          54600.0f // 54.60 V at 100% SoC
#define SIM_BATT_EMPTY_MV         42000.0f // 42.00 V at 0% SoC
#define SIM_BATT_DRAIN_CYCLE_SEC  300.0f   // 5-minute (300s) accelerated drain (100% -> 10% SoC)
#define SIM_BATT_IDLE_AMPS        1.0f     // Base electronics/idle draw (A)
#define SIM_BATT_PEAK_LOAD_AMPS   11.0f    // Additional load current at 10 mph (up to 12.0 A total)
#define SIM_BATT_INTERNAL_RES_OHM 0.125f   // Internal resistance (~1.5V sag at 12A peak load)

// -----------------------------------------------------------------------------
// 3. Hardware Pin Assignments (XIAO ESP32S3 + Wio-SX1262)
// -----------------------------------------------------------------------------
// Wio-SX1262 B2B LoRa Radio Pins
#define LORA_LDO_EN_PIN           38      // RF Switch / LDO Enable (Active HIGH)
#define LORA_NSS_PIN              41      // SPI Chip Select
#define LORA_DIO1_PIN             39      // Hardware Interrupt (DIO1)
#define LORA_NRST_PIN             42      // Radio Reset
#define LORA_BUSY_PIN             40      // Radio Busy Status

// External Trike Sensors (Future-Proof Hardware Pins)
#define HALL_SENSOR_PIN           1       // Header D0 (GPIO 1) - RISING interrupt in IRAM
#define I2C_SDA_PIN               5       // Header D4 (GPIO 5) - I2C SDA for INA228
#define I2C_SCL_PIN               6       // Header D5 (GPIO 6) - I2C SCL for INA228
#define INA228_I2C_ADDR           0x40    // Default TI INA228 I2C Address

// -----------------------------------------------------------------------------
// 4. Wheel Geometry & Calibration Constants
// -----------------------------------------------------------------------------
#define DEFAULT_WHEEL_DIAMETER_IN 20.0f   // 20-inch rear wheel (50-ft straight-line calibrated)
#define HALL_PULSES_PER_REV       1       // 1 magnet per wheel revolution
#define HALL_DEBOUNCE_US          15000UL // 15 ms minimum pulse spacing (~80 mph max ceiling)
#define WHEEL_TIMEOUT_MS          1500UL  // Zero speed if no Hall pulse for 1.5 seconds

// -----------------------------------------------------------------------------
// 5. Wi-Fi UDP (iPhone ZIG SIM) & LoRa RF Settings
// -----------------------------------------------------------------------------
#define TX_AP_SSID                "ASME-Trike-TX"
#define ZIG_UDP_PORT              50000

// High-Rate LoRa Parameters (~8.2 ms airtime)
#define LORA_FREQ_MHZ             915.0f
#define LORA_BW_KHZ               500.0f
#define LORA_SF                   7
#define LORA_CR                   5
#define LORA_PWR_DBM              2       // 2 dBm prevents breadboard 5V rail brownout & close-range LNA overload
#define LORA_PREAMBLE_LEN         8
#define LORA_TCXO_VOLTS           1.6f

#define LORA_TX_INTERVAL_MS       97      // ~10.3 Hz (97 ms avoids phase-locking with 102.4 ms Wi-Fi AP beacons!)
#define RAM_LOG_RATE_HZ           60      // 60 Hz local RAM logging rate
#define RAM_LOG_INTERVAL_US       16667UL // 16,667 us = 60.0 Hz sample period
#define MAX_RAM_LOG_SAMPLES       540000UL // 2.5 hours (9,000s) @ 60 Hz = 540,000 samples (6.18 MB in 8 MB PSRAM)
#define FLASH_LOG_INTERVAL_MS     1000    // 1 Hz non-volatile LittleFS backup checkpoint
#define MAX_FLASH_RECORDS         10800   // 3 hours * 3600 seconds/hour
