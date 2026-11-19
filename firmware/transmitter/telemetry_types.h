/*
  =============================================================================
  telemetry_types.h — Shared Telemetry Structs, Flags & Binary LoRa Protocol
  Must remain byte-for-byte identical between Transmitter and Receiver.
  =============================================================================
*/

#pragma once
#include <Arduino.h>

// Status Bitmask Flags (LoRaTelemetryPacket::flags)
#define FLAG_PHONE_WIFI_CONN  0x01  // iPhone is connected to "ASME-Trike-TX" SoftAP
#define FLAG_ZIG_STREAMING    0x02  // ZIG SIM UDP packets actively arriving (< 500 ms ago)
#define FLAG_HAS_QUAT         0x04  // Valid 3D Quaternion reference pose locked
#define FLAG_SIM_SPEED        0x08  // Wheel speed is running in 10s Sinusoidal Sim mode
#define FLAG_SIM_BATTERY      0x10  // INA228 power is running in 48V Sinusoidal Sim mode
#define FLAG_FLASH_OK         0x20  // Onboard LittleFS 3-Hour flash logger mounted & active
#define FLAG_RACE_RUNNING     0x40  // Race timer & wheel speed simulation actively running (not paused)

// Command IDs for Pitwall Receiver -> Transmitter Control Packet
#define CMD_SET_PAUSED        0x00  // Pause timer, wheel speed (0 mph) & battery drain
#define CMD_SET_RUNNING       0x01  // Start/resume timer, 10s sinusoidal speed & battery drain
#define CMD_RESET_RACE        0x02  // Reset timer, distance, energy & SoC to initial paused state

// -----------------------------------------------------------------------------
// 4-Byte Compact LoRa Control Packet (Receiver -> Transmitter during RX slot)
// -----------------------------------------------------------------------------
struct __attribute__((packed)) LoRaCommandPacket {
  uint8_t magic;              // 0xB6 command identifier
  uint8_t cmd;                // CMD_SET_PAUSED, CMD_SET_RUNNING, or CMD_RESET_RACE
  uint8_t reset_seq;          // Increments on each reset command to deduplicate
  uint8_t checksum;           // magic ^ cmd ^ reset_seq
};

// -----------------------------------------------------------------------------
// 44-Byte Packed Binary LoRa Telemetry Packet (Transmitter -> Receiver @ 10 Hz)
// All sensor fusion, 2D dead-reckoning (posX, posY) & PSRAM logging run at 60 Hz
// on the Transmitter; 10 Hz LoRa packets stream live state snapshots to Pitwall.
// -----------------------------------------------------------------------------
struct __attribute__((packed)) LoRaTelemetryPacket {
  uint8_t  magic;             // 0xA5 packet identifier
  uint8_t  flags;             // Status bitmask (Phone, ZIG, Quat, SimSpeed, SimBatt, Flash, Running)
  uint16_t seq;               // Rolling sequence number (incremented on actual LoRa TX)
  uint32_t uptime_ms;         // Active race elapsed time in milliseconds

  // 1. Live Phone IMU Attitude (from ZIG SIM 60 Hz Quaternion)
  uint16_t heading_x10;       // 0 .. 3600 (0.0 .. 360.0 deg relative yaw)
  int16_t  roll_x10;          // -1800 .. +1800 (-180.0 .. +180.0 deg lean angle)
  int16_t  pitch_x10;         // -900 .. +900 (-90.0 .. +90.0 deg slope angle)
  int16_t  lat_g_x100;        // Lateral cornering G-force * 100 (e.g. 38 -> 0.38 G)

  // 2. Wheel Speed, Distance & 60 Hz Transmitter-Integrated 2D Position
  uint16_t speed_mph_x100;    // Speed * 100 (400 .. 1200 -> 4.00 .. 12.00 mph)
  uint16_t wheel_rpm_x10;     // Wheel RPM * 10 (0 .. 6000 -> 0.0 .. 600.0 RPM)
  uint32_t distance_ft_x10;   // Cumulative 60 Hz integrated distance in 0.1 ft units
  int32_t  pos_x_ft_x10;      // 60 Hz Transmitter-integrated X position in 0.1 ft units
  int32_t  pos_y_ft_x10;      // 60 Hz Transmitter-integrated Y position in 0.1 ft units

  // 3. INA228 48V Battery & Power Telemetry (Real I2C or Sinusoidal Drain Sim)
  uint16_t voltage_mv;        // Pack voltage in mV (e.g. 54600 = 54.60 V, 42000 = 42.00 V)
  int16_t  current_ma_x10;    // Load current in 10 mA units (e.g. 1140 = 11.40 A)
  uint16_t energy_wh_x10;     // Cumulative energy used in 0.1 Wh units
  uint16_t soc_x10;           // Battery State of Charge * 10 (0 .. 1000 -> 0.0% .. 100.0%)

  // 4. Onboard 60 Hz RAM Log & Link Diagnostics
  uint16_t tx_records;        // 60 Hz RAM samples / 10 (0 .. 54,000 -> 0 .. 540,000 samples)
  uint8_t  zig_hz;            // Measured ZIG SIM UDP packet rate on Transmitter (Hz)
  int8_t   phone_rssi;        // iPhone Wi-Fi RSSI at Transmitter (dBm)
};

// -----------------------------------------------------------------------------
// 12-Byte Packed 60 Hz Local RAM Telemetry Sample (Stored in 8 MB OPI PSRAM)
// 540,000 samples (2.5 hours @ 60 Hz) * 12 bytes = 6,480,000 bytes (6.18 MB)
// Leaves ~1.9 MB of free PSRAM + ~285 KB of free internal SRAM on XIAO ESP32S3!
// -----------------------------------------------------------------------------
struct __attribute__((packed)) RamLogRecord60Hz {
  uint16_t speed_mph_x100;    // 2B: Wheel speed * 100 (0.00 .. 655.35 mph)
  uint16_t yaw_deg_x10;       // 2B: Relative heading * 10 (0.0 .. 360.0 deg)
  int16_t  pos_x_ft_x10;      // 2B: 60 Hz integrated X position in 0.1 ft (-3276.8..+3276.7 ft)
  int16_t  pos_y_ft_x10;      // 2B: 60 Hz integrated Y position in 0.1 ft (-3276.8..+3276.7 ft)
  int8_t   roll_deg;          // 1B: Roll angle in deg (-128 .. +127 deg)
  int8_t   pitch_deg;         // 1B: Pitch angle in deg (-128 .. +127 deg)
  uint8_t  voltage_dV;        // 1B: Pack voltage in 0.25V steps (e.g. 218 = 54.5V)
  uint8_t  current_dA;        // 1B: Motor current in 0.1A steps (e.g. 115 = 11.5A)
};

// -----------------------------------------------------------------------------
// 16-Byte Compact Onboard Flash Record (Stored at 1 Hz in /tx_log.bin)
// 10,800 records (3 hours) * 16 bytes = 172.8 KB (< 6% of LittleFS Flash)
// -----------------------------------------------------------------------------
struct __attribute__((packed)) FlashLogRecord {
  uint16_t seq_num;           // Record index (0 .. 10800)
  uint16_t timestamp_s;       // Elapsed active race seconds
  uint16_t speed_mph_x100;    // Wheel speed * 100
  uint16_t wheel_rpm_x10;     // Wheel RPM * 10
  int16_t  roll_x10;          // Roll deg * 10
  int16_t  pitch_x10;         // Pitch deg * 10
  uint16_t yaw_deg_x10;       // Heading deg * 10
  uint8_t  voltage_dV;        // Pack voltage in 0.25V steps (e.g. 200 = 50.0V)
  uint8_t  soc_pct;           // Battery SoC (0 .. 100%)
};

// -----------------------------------------------------------------------------
// Master Transmitter State Structure (Updated by Modular Sensor Drivers)
// -----------------------------------------------------------------------------
struct TrikeTelemetryState {
  // Mode & Run/Pause switches (Transmitter continuously broadcasts sensor/sim waves; Receiver gates Start/Pause)
  bool     raceRunning      = true;
  uint32_t activeSimTimeMs  = 0;
  bool     simWheelSpeed    = false;
  bool     simBattery       = true;
  bool     flashReady       = false;
  bool     ramLogReady      = false;

  // 1. Phone IMU State (ZIG SIM)
  float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
  float refQx = 0.0f, refQy = 0.0f, refQz = 0.0f, refQw = 1.0f;
  float refCompassOffsetDeg = 0.0f;
  bool  hasRefQuat     = false;
  bool  hasQuat        = false;
  float headingDeg     = 0.0f;
  float prevHeadingDeg = 0.0f;
  float yawRateDegPerS = 0.0f;
  float rollDeg        = 0.0f;
  float pitchDeg       = 0.0f;
  float lateralG       = 0.0f;
  uint32_t quatCount   = 0;

  // Phone Wi-Fi & UDP Diagnostics
  unsigned long lastUdpTimeMs = 0;
  uint16_t udpPacketsThisSec  = 0;
  uint8_t  measuredZigHz      = 0;
  bool     phoneWifiConnected = false;
  int8_t   phoneWifiRssi      = -127;

  // 2. Wheel Speed & Distance State
  float wheelDiameterIn = 20.0f;
  float speedMph        = 0.0f;
  float speedFts        = 0.0f;
  float wheelRpm        = 0.0f;
  float totalDistanceFt = 0.0f;

  // 3. INA228 48V Power & Battery State
  float voltageV        = 54.6f;
  float currentA        = 0.0f;
  float powerW          = 0.0f;
  float energyWh        = 0.0f;
  float socPct          = 100.0f;

  // 4. Dead-Reckoning / Kalman Navigation State (on Trike)
  float posXFt          = 0.0f;
  float posYFt          = 0.0f;
  float uncertaintyFt   = 1.5f;

  // 5. Onboard 60 Hz RAM Logger (540,000 samples = 2.5 hrs) & 1 Hz Flash Backup
  uint32_t txRamSamples60Hz   = 0;
  uint32_t txRamCapacity      = 0;
  uint32_t txFreePsramBytes   = 0;
  uint16_t txRecordsLogged    = 0;
};
