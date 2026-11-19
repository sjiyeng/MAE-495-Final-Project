/*
  =============================================================================
  ASME e-HPVC Telemetry System - Wheel Speed Sensor Hardware Test (DIO Pin 1)
  Target Hardware: Seeed Studio XIAO ESP32S3
  =============================================================================

  HOW IT WORKS:
  1. Attaches IRAM hardware interrupts (RISING edge, 15 ms debounce) on:
       - GPIO 1 (Header D0, matching HALL_SENSOR_PIN in firmware/transmitter/config.h)
       - Header D1 (GPIO 2 on XIAO ESP32S3) as an automatic fallback if wired to D1
  2. Computes wheel RPM, speed (mph & ft/s), total distance (ft), and pulse period.
  3. Writes sensor telemetry to USB Serial (115200 baud) once per 50 ms cycle (20 Hz).
  4. Send 'r' or 'R' over Serial to reset pulse count and total distance to zero.
  =============================================================================
*/

#include <Arduino.h>
#include <math.h>

// -----------------------------------------------------------------------------
// Pin & Wheel Calibration Constants (Matched to firmware/transmitter/config.h)
// -----------------------------------------------------------------------------
#define WHEEL_SENSOR_PIN     1       // DIO Pin 1 / GPIO 1 (Header D0)
#define WHEEL_SENSOR_D1_PIN  D1      // Silkscreen Header D1 (GPIO 2 on XIAO ESP32S3)

#define WHEEL_DIAMETER_IN    20.0f   // 20-inch rear wheel
#define HALL_PULSES_PER_REV  1       // 1 magnet per wheel revolution
#define HALL_DEBOUNCE_US     15000UL // 15 ms minimum pulse spacing
#define WHEEL_TIMEOUT_MS     1500UL  // Zero speed if no pulse for 1.5 seconds
#define CYCLE_INTERVAL_MS    50UL    // 20 Hz deterministic print cycle

// -----------------------------------------------------------------------------
// ISR State for GPIO 1 (Pin 1) and Header D1 (GPIO 2)
// -----------------------------------------------------------------------------
static volatile uint32_t pin1PulseCount      = 0;
static volatile uint32_t pin1LastPulseUs     = 0;
static volatile uint32_t pin1LatestPeriodUs  = 0;

static volatile uint32_t pinD1PulseCount     = 0;
static volatile uint32_t pinD1LastPulseUs    = 0;
static volatile uint32_t pinD1LatestPeriodUs = 0;

static uint32_t prevActivePulses  = 0;
static uint8_t  prevActiveSource  = 0; // 0 = Pin 1 (GPIO 1), 1 = Header D1 (GPIO 2)
static float    totalDistanceFt   = 0.0f;
static uint32_t cycleCount        = 0;
static unsigned long lastCycleMs  = 0;

#if defined(ESP32)
IRAM_ATTR
#endif
void onPin1Pulse() {
  uint32_t nowUs = micros();
  uint32_t dtUs  = nowUs - pin1LastPulseUs;
  if (dtUs >= HALL_DEBOUNCE_US) {
    if (pin1PulseCount > 0) {
      pin1LatestPeriodUs = dtUs;
    }
    pin1LastPulseUs = nowUs;
    pin1PulseCount++;
  }
}

#if defined(ESP32)
IRAM_ATTR
#endif
void onPinD1Pulse() {
  uint32_t nowUs = micros();
  uint32_t dtUs  = nowUs - pinD1LastPulseUs;
  if (dtUs >= HALL_DEBOUNCE_US) {
    if (pinD1PulseCount > 0) {
      pinD1LatestPeriodUs = dtUs;
    }
    pinD1LastPulseUs = nowUs;
    pinD1PulseCount++;
  }
}

void resetSensorStats() {
  noInterrupts();
  pin1PulseCount      = 0;
  pin1LastPulseUs     = 0;
  pin1LatestPeriodUs  = 0;
  pinD1PulseCount     = 0;
  pinD1LastPulseUs    = 0;
  pinD1LatestPeriodUs = 0;
  interrupts();

  prevActivePulses = 0;
  totalDistanceFt  = 0.0f;
}

void handleSerialCommands() {
  while (Serial.available() > 0) {
    char cmd = (char)Serial.read();
    if (cmd == 'r' || cmd == 'R') {
      resetSensorStats();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);

  pinMode(WHEEL_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(WHEEL_SENSOR_PIN), onPin1Pulse, RISING);

  if (WHEEL_SENSOR_D1_PIN != WHEEL_SENSOR_PIN) {
    pinMode(WHEEL_SENSOR_D1_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(WHEEL_SENSOR_D1_PIN), onPinD1Pulse, RISING);
  }

  lastCycleMs = millis();

  if (Serial) {
    Serial.println(F("=========================================================="));
    Serial.println(F("  XIAO ESP32S3 Wheel Speed Sensor Test (DIO Pin 1 / D1)   "));
    Serial.println(F("  Send 'r' over Serial to reset pulse count & distance.   "));
    Serial.println(F("=========================================================="));
  }
}

void loop() {
  handleSerialCommands();

  unsigned long nowMs = millis();
  if (nowMs - lastCycleMs < CYCLE_INTERVAL_MS) {
    return;
  }
  lastCycleMs += CYCLE_INTERVAL_MS;
  if (nowMs - lastCycleMs >= CYCLE_INTERVAL_MS) {
    lastCycleMs = nowMs;
  }

  cycleCount++;

  // Snapshot ISR counters atomically
  noInterrupts();
  uint32_t p1Count  = pin1PulseCount;
  uint32_t p1LastUs = pin1LastPulseUs;
  uint32_t p1PerUs  = pin1LatestPeriodUs;

  uint32_t d1Count  = pinD1PulseCount;
  uint32_t d1LastUs = pinD1LastPulseUs;
  uint32_t d1PerUs  = pinD1LatestPeriodUs;
  interrupts();

  // Select active sensor source (GPIO 1 primary, Header D1 fallback if only D1 has pulses)
  uint8_t  activeSource = 0;
  uint32_t pulses       = p1Count;
  uint32_t lastUs       = p1LastUs;
  uint32_t periodUs     = p1PerUs;

  uint32_t nowUs = micros();
  uint32_t p1AgeMs = (p1Count > 0) ? ((nowUs - p1LastUs) / 1000UL) : UINT32_MAX;
  uint32_t d1AgeMs = (d1Count > 0) ? ((nowUs - d1LastUs) / 1000UL) : UINT32_MAX;

  if (p1Count == 0 && d1Count > 0) {
    activeSource = 1;
  } else if (d1Count > 0 && p1AgeMs > WHEEL_TIMEOUT_MS && d1AgeMs <= WHEEL_TIMEOUT_MS) {
    activeSource = 1;
  }

  if (activeSource == 1) {
    pulses   = d1Count;
    lastUs   = d1LastUs;
    periodUs = d1PerUs;
  }

  if (activeSource != prevActiveSource) {
    prevActiveSource = activeSource;
    prevActivePulses = pulses;
  }

  float circumferenceFt = (PI * WHEEL_DIAMETER_IN) / 12.0f;
  uint32_t newPulses = (pulses >= prevActivePulses) ? (pulses - prevActivePulses) : pulses;
  prevActivePulses = pulses;

  if (newPulses > 0) {
    totalDistanceFt += ((float)newPulses / (float)HALL_PULSES_PER_REV) * circumferenceFt;
  }

  float rpm      = 0.0f;
  float speedFts = 0.0f;
  float speedMph = 0.0f;
  float periodMs = 0.0f;

  uint32_t ageMs = (pulses > 0) ? ((nowUs - lastUs) / 1000UL) : UINT32_MAX;
  if (pulses > 0 && ageMs <= WHEEL_TIMEOUT_MS && periodUs > 0) {
    float revPerSec = 1000000.0f / ((float)periodUs * (float)HALL_PULSES_PER_REV);
    rpm      = revPerSec * 60.0f;
    speedFts = revPerSec * circumferenceFt;
    speedMph = speedFts * 0.6818182f;
    periodMs = periodUs * 0.001f;
  }

  int pin1State  = digitalRead(WHEEL_SENSOR_PIN);
  int pinD1State = digitalRead(WHEEL_SENSOR_D1_PIN);

  if (Serial && Serial.availableForWrite() >= 128) {
    Serial.printf(
      "Cycle:%lu,Pin1:%d,PinD1:%d,Pulses:%lu,RPM:%.1f,SpeedMph:%.2f,SpeedFts:%.2f,DistFt:%.1f,PeriodMs:%.2f\n",
      (unsigned long)cycleCount,
      pin1State,
      pinD1State,
      (unsigned long)pulses,
      rpm,
      speedMph,
      speedFts,
      totalDistanceFt,
      periodMs
    );
  }
}
