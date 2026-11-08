/*
  =============================================================================
  ASME e-HPVC Telemetry & Race Management System — Trike Transmitter Firmware
  Target Hardware: Seeed Studio XIAO ESP32S3 + Wio-SX1262 (B2B Connector)

  Modular Architecture (Arduino IDE Tabs):
    - transmitter.ino   : Main setup() & non-blocking loop() orchestrator
    - config.h          : Hardware pins, RF parameters & simulation switches
    - telemetry_types.h : Shared binary packet & telemetry state structs
    - imu_zig_sim.h     : 60 Hz iPhone ZIG SIM Wi-Fi UDP & 3D Quaternion lock
    - wheel_sensor.h    : GPIO 1 Hall ISR + 10s Sinusoidal (0-10 mph) simulator
    - power_ina228.h    : I2C INA228 driver + 48V Sinusoidal/Drain simulator
    - kalman_nav.h      : 2D Dead-Reckoning & loop-closure filter
    - flash_logger.h    : 3-Hour (10,800s) LittleFS binary flash logger
    - lora_tx.h         : Non-blocking 915 MHz LoRa telemetry transmitter & cmd RX
  =============================================================================
*/

#include <Arduino.h>
#include "config.h"
#include "telemetry_types.h"
#include "imu_zig_sim.h"
#include "wheel_sensor.h"
#include "power_ina228.h"
#include "kalman_nav.h"
#include "flash_logger.h"
#include "lora_tx.h"

TrikeTelemetryState trikeState;

static unsigned long lastSensorUs = 0;
static unsigned long lastTxMs     = 0;
static unsigned long lastDiagMs   = 0;

void handleSerialCommands() {
  while (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'z' || cmd == 'Z') {
      zeroCurrentHeading();
    } else if (cmd == 'c' || cmd == 'C') {
      resetCompassHeading();
    } else if (cmd == 'p' || cmd == 'P') {
      trikeState.raceRunning = true;
    } else if (cmd == 'q' || cmd == 'Q') {
      trikeState.raceRunning = false;
      trikeState.speedMph    = 0.0f;
      trikeState.speedFts    = 0.0f;
      trikeState.wheelRpm    = 0.0f;
    } else if (cmd == 't' || cmd == 'T') {
      resetEntireRaceStateOnTransmitter();
    } else if (cmd == 's' || cmd == 'S') {
      trikeState.simWheelSpeed = !trikeState.simWheelSpeed;
    } else if (cmd == 'b' || cmd == 'B') {
      trikeState.simBattery = !trikeState.simBattery;
    } else if (cmd == 'e' || cmd == 'E') {
      clearFlashLog();
    }
  }
}

void printNonBlockingSerialTelemetry(unsigned long nowMs) {
  if (!Serial || Serial.availableForWrite() < 160) return;

  bool isZigStreaming = (trikeState.lastUdpTimeMs > 0) && (nowMs - trikeState.lastUdpTimeMs <= 500);
  Serial.printf(
    "Running:%d,RaceMs:%lu,Heading:%.1f,Roll:%.1f,Pitch:%.1f,LatG:%.2f,Speed:%.2f,RPM:%.1f,DistFt:%.1f,"
    "Volt:%.2f,Curr:%.2f,EnergyWh:%.1f,SoC:%.1f,PosX:%.1f,PosY:%.1f,"
    "PhoneConn:%d,Streaming:%d,ZigHz:%u,PhoneRSSI:%d,SimSpd:%d,SimBat:%d,TxRec:%u\n",
    trikeState.raceRunning ? 1 : 0,
    (unsigned long)trikeState.activeSimTimeMs,
    trikeState.headingDeg, trikeState.rollDeg, trikeState.pitchDeg, trikeState.lateralG,
    trikeState.speedMph, trikeState.wheelRpm, trikeState.totalDistanceFt,
    trikeState.voltageV, trikeState.currentA, trikeState.energyWh, trikeState.socPct,
    trikeState.posXFt, trikeState.posYFt,
    trikeState.phoneWifiConnected ? 1 : 0,
    isZigStreaming ? 1 : 0,
    isZigStreaming ? trikeState.measuredZigHz : 0,
    (int)trikeState.phoneWifiRssi,
    trikeState.simWheelSpeed ? 1 : 0,
    trikeState.simBattery ? 1 : 0,
    (unsigned)trikeState.txRecordsLogged
  );
}

void setup() {
  Serial.begin(115200);
  delay(400);

  initFlashLogger();
  initWheelSensor();
  initPowerSensor();
  initKalmanNav();
  initImuZigSim();
  initLoRaTransmitter();
}

void loop() {
  unsigned long nowMs = millis();
  unsigned long nowUs = micros();

  // 1. Poll USB Serial commands & high-rate 60 Hz ZIG SIM UDP packets
  handleSerialCommands();
  pollImuZigSim();

  // 2. Run 60 Hz (16.67 ms) Sensor Update, Simulation & 60 Hz Local PSRAM Logging (540,000 samples / 2.5 hrs)
  if ((nowUs - lastSensorUs) >= RAM_LOG_INTERVAL_US) {
    if ((nowUs - lastSensorUs) > (RAM_LOG_INTERVAL_US * 4UL)) {
      lastSensorUs = nowUs;
    } else {
      lastSensorUs += RAM_LOG_INTERVAL_US;
    }

    updateWheelSensor(nowMs);
    updatePowerSensor(nowMs);
    updateKalmanNav(nowMs);
    updateRamLogger60Hz(nowUs);
  }

  // 3. Update 1 Hz Phone Wi-Fi RSSI (60 Hz PSRAM logger handles all data logging without Flash stalls!)
  if (nowMs - lastDiagMs >= 1000) {
    lastDiagMs = nowMs;
    updatePhoneWifiDiagnostics1Hz();
  }

  // 4. Service non-blocking LoRa radio completion & 40 Hz (25 ms) Telemetry Broadcast
  pollLoRaTxState(nowMs);

  if (nowMs - lastTxMs >= LORA_TX_INTERVAL_MS) {
    lastTxMs = nowMs;
    transmitTelemetryPacket(nowMs);
    printNonBlockingSerialTelemetry(nowMs);
  }
}
