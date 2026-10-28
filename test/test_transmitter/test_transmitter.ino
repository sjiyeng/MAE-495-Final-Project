/*
  =============================================================================
  ASME e-HPVC Telemetry System - LoRa Transmitter Test (10 Hz)
  Target Hardware: Seeed Studio XIAO ESP32S3 + Wio-SX1262 (B2B Connector)
  Library Required: RadioLib (by Jan Gromes) via Arduino Library Manager
  =============================================================================
*/

#include <RadioLib.h>

// -----------------------------------------------------------------------------
// Pin Definitions for Seeed XIAO ESP32S3 + Wio-SX1262 B2B Connector
// -----------------------------------------------------------------------------
#define LORA_LDO_EN_PIN   38   // Power / RF switch enable on Wio-SX1262
#define LORA_NSS_PIN      41   // SPI Chip Select
#define LORA_DIO1_PIN     39   // Interrupt (DIO1)
#define LORA_NRST_PIN     42   // Hardware Reset
#define LORA_BUSY_PIN     40   // Busy status line

// Instantiate RadioLib SX1262 module: Module(nss, dio1, nrst, busy)
SX1262 radio = new Module(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_NRST_PIN, LORA_BUSY_PIN);

// -----------------------------------------------------------------------------
// RF Configuration (Fast LoRa tuned for 10 Hz high-throughput transmission)
// -----------------------------------------------------------------------------
const float LORA_FREQ_MHZ = 915.0;     // Frequency in MHz (US915)
const float LORA_BW_KHZ   = 250.0;     // Bandwidth 250 kHz for low airtime (~14 ms)
const uint8_t LORA_SF     = 7;         // Spreading Factor 7 (fast airtime)
const uint8_t LORA_CR     = 5;         // Coding Rate 4/5
const uint8_t LORA_SYNC   = RADIOLIB_SX126X_SYNC_WORD_PRIVATE;
const int8_t  LORA_PWR    = 14;        // Output power in dBm
const float   LORA_TCXO   = 1.6;       // 1.6V reference for Wio-SX1262 TCXO

// Timing & packet counter for 10 Hz
unsigned long lastTxTime = 0;
const unsigned long TX_INTERVAL_MS = 100; // 10 Hz (every 100 ms)
uint32_t packetCounter = 0;

void setup() {
  Serial.begin(115200);
  delay(1500); // Give Serial monitor time to connect

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  XIAO ESP32S3 + Wio-SX1262 TRANSMITTER "));
  Serial.println(F("     10 Hz High-Speed LoRa Test         "));
  Serial.println(F("========================================"));

  // 1. Enable power / RF switch to Wio-SX1262
  pinMode(LORA_LDO_EN_PIN, OUTPUT);
  digitalWrite(LORA_LDO_EN_PIN, HIGH);
  delay(50);

  // 2. Configure TCXO voltage (crucial for Wio-SX1262 stability)
  radio.tcxoVoltage = LORA_TCXO;

  // 3. Initialize SX1262 radio with fast parameters for 10 Hz
  Serial.print(F("[SX1262] Initializing radio (915MHz, BW 250kHz, SF7) ... "));
  int state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC, LORA_PWR);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("SUCCESS!"));
  } else {
    Serial.print(F("FAILED, code "));
    Serial.println(state);
    Serial.println(F("Check B2B connection and library version."));
    while (true) {
      delay(1000);
    }
  }

  Serial.println(F("[SX1262] Starting 10 Hz transmission loop (every 100 ms)..."));
}

void loop() {
  unsigned long now = millis();

  // Send packet every 100 ms (10 Hz non-blocking)
  if (now - lastTxTime >= TX_INTERVAL_MS) {
    lastTxTime = now;
    packetCounter++;

    // Prepare packet message
    String message = "Hello World #" + String(packetCounter);

    unsigned long startTx = millis();
    int state = radio.transmit(message);
    unsigned long txDuration = millis() - startTx;

    if (state == RADIOLIB_ERR_NONE) {
      Serial.print(F("[TX #"));
      Serial.print(packetCounter);
      Serial.print(F("] Sent in "));
      Serial.print(txDuration);
      Serial.println(F(" ms"));
    } else {
      Serial.print(F("[TX #"));
      Serial.print(packetCounter);
      Serial.print(F("] FAILED, code "));
      Serial.println(state);
    }
  }
}
