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
// Receiver State Structure (Updated on Core 1, Read by WebServer on Core 0)
// -----------------------------------------------------------------------------
struct ReceiverState {
  // Run/Pause Control State (Starts PAUSED on boot!)
  bool     desiredRaceRunning = false;
  bool     txRaceRunning      = false;
  uint32_t activeRaceTimeMs   = 0;
  uint8_t  resetSeq           = 0;
  uint8_t  pendingResetBursts = 0;
  uint8_t  lastWebZeroSeq     = 0;
  uint8_t  lastWebResetSeq    = 0;
  uint8_t  lastWebLapSeq      = 0;

  // Attitude & Orientation
  float rawHeadingDeg     = 0.0f;
  float displayHeadingDeg = 0.0f;
  float zeroOffsetDeg     = 0.0f;
  float rollDeg           = 0.0f;
  float pitchDeg          = 0.0f;
  float lateralG          = 0.0f;

  // Wheel Speed & Distance
  float wheelDiameterIn   = 20.0f;
  float speedMph          = 0.0f;
  float speedFts          = 0.0f;
  float wheelRpm          = 0.0f;
  float totalDistanceFt   = 0.0f;
  float prevTxDistanceFt  = 0.0f;
  bool  hasInitialDist    = false;
  float lapDistanceFt     = 0.0f;

  // 2D Dead-Reckoning / Kalman Coordinates (ft, relative to Start Pin 0,0)
  float posXFt            = 0.0f;
  float posYFt            = 0.0f;
  float prevTxPosXFt      = 0.0f;
  float prevTxPosYFt      = 0.0f;
  bool  hasInitialTxPos   = false;
  float uncertaintyFt     = 1.5f;

  // 48V INA228 Battery & Power
  float voltageV          = 54.6f;
  float currentA          = 0.0f;
  float powerW            = 0.0f;
  float energyWh          = 0.0f;
  float socPct            = 100.0f;

  // Sensor & 60 Hz RAM Log Status (540,000 samples = 2.5 hrs @ 60 Hz)
  bool     phoneWifiConnected = false;
  bool     zigStreaming       = false;
  bool     hasQuatPose        = false;
  bool     simSpeedActive     = true;
  bool     simBatteryActive   = true;
  bool     txFlashOk          = false;
  bool     rxRamLogReady      = false;
  uint32_t txFlashRecords     = 0;
  uint32_t rxFlashRecords     = 0;
  uint32_t rxRamCapacity      = 0;
  uint8_t  zigUdpHz           = 0;
  int8_t   phoneWifiRssi      = -127;

  // LoRa RF & Packet Loss Statistics (< 2.0% Target)
  float    rssi               = -127.0f;
  float    snr                = 0.0f;
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
