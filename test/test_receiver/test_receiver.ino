/*
  =============================================================================
  ASME e-HPVC Telemetry System - LoRa Receiver Test (10 Hz)
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
// RF Configuration (Must match transmitter exactly for 10 Hz operation)
// -----------------------------------------------------------------------------
const float LORA_FREQ_MHZ = 915.0;     // Frequency in MHz (US915)
const float LORA_BW_KHZ   = 250.0;     // Bandwidth 250 kHz
const uint8_t LORA_SF     = 7;         // Spreading Factor 7
const uint8_t LORA_CR     = 5;         // Coding Rate 4/5
const uint8_t LORA_SYNC   = RADIOLIB_SX126X_SYNC_WORD_PRIVATE;
const int8_t  LORA_PWR    = 14;        // Output power in dBm
const float   LORA_TCXO   = 1.6;       // 1.6V reference for Wio-SX1262 TCXO

// Flag set by hardware interrupt when a packet arrives
volatile bool receivedFlag = false;

// Interrupt Service Routine (IRAM attribute for ESP32)
#if defined(ESP32) || defined(ESP8266)
IRAM_ATTR
#endif
void onPacketReceived(void) {
  receivedFlag = true;
}

// Receiver statistics
uint32_t totalPacketsReceived = 0;
unsigned long lastPacketTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1500); // Give Serial monitor time to connect

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("   XIAO ESP32S3 + Wio-SX1262 RECEIVER   "));
  Serial.println(F("     10 Hz High-Speed LoRa Test         "));
  Serial.println(F("========================================"));

  // 1. Enable power / RF switch to Wio-SX1262
  pinMode(LORA_LDO_EN_PIN, OUTPUT);
  digitalWrite(LORA_LDO_EN_PIN, HIGH);
  delay(50);

  // 2. Configure TCXO voltage (crucial for Wio-SX1262 stability)
  radio.tcxoVoltage = LORA_TCXO;

  // 3. Initialize SX1262 radio with matching 10 Hz parameters
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

  // 4. Attach DIO1 interrupt handler
  radio.setDio1Action(onPacketReceived);

  // 5. Put radio into continuous listening mode
  Serial.print(F("[SX1262] Listening for incoming packets ... "));
  state = radio.startReceive();
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("READY!"));
  } else {
    Serial.print(F("FAILED, code "));
    Serial.println(state);
    while (true) {
      delay(1000);
    }
  }

  Serial.println(F("-----------------------------------------------------------------"));
  Serial.println(F("Count | Payload               | RSSI (dBm) | SNR (dB) | Delta (ms)"));
  Serial.println(F("-----------------------------------------------------------------"));
}

void loop() {
  if (receivedFlag) {
    receivedFlag = false;

    String rxPayload;
    int state = radio.readData(rxPayload);

    if (state == RADIOLIB_ERR_NONE) {
      totalPacketsReceived++;
      unsigned long now = millis();
      unsigned long interval = (lastPacketTime == 0) ? 0 : (now - lastPacketTime);
      lastPacketTime = now;

      float rssi = radio.getRSSI();
      float snr  = radio.getSNR();

      // Clean single-line printout formatted for high-frequency (10 Hz) streams
      char lineBuffer[120];
      snprintf(lineBuffer, sizeof(lineBuffer), "#%-5u | %-21s | %6.1f dBm | %5.1f dB | %4lu ms",
               (unsigned int)totalPacketsReceived,
               rxPayload.c_str(),
               rssi,
               snr,
               interval);
      Serial.println(lineBuffer);

    } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
      Serial.println(F("[RX ERROR] CRC mismatch!"));
    } else {
      Serial.print(F("[RX ERROR] readData failed, code: "));
      Serial.println(state);
    }

    // Immediately return radio to receive mode for the next 10 Hz packet
    radio.startReceive();
  }
}
