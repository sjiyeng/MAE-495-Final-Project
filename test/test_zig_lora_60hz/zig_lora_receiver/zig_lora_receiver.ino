/*
  =============================================================================
  ASME e-HPVC Telemetry System - 60 Hz LoRa Receiver + Serial & Web Dashboard
  Target Hardware: Seeed Studio XIAO ESP32S3 + Wio-SX1262 (B2B Connector)
  Libraries Required:
    - RadioLib (by Jan Gromes) via Arduino Library Manager
    - Built-in ESP32 WiFi & WebServer libraries
  =============================================================================
  
  HOW IT WORKS:
  1. Receives 60 Hz 9-byte binary LoRa packets (915 MHz, BW 500 kHz, SF7) from
     the Transmitter via DIO1 hardware interrupts.
  2. Outputs 60 Hz Heading + RSSI/SNR to USB Serial so it can be visualized
     directly in `compass_plotter.html` (Chrome/Edge Web Serial) or Arduino
     Serial Plotter.
  3. Supports 'z' (Zero Heading) and 'c' (Compass Reset) commands sent from
     `compass_plotter.html` over USB Serial.
  4. Also hosts a standalone Wi-Fi AP ("ASME-Pitwall-RX" -> http://192.168.4.1)
     serving the same live 2D Circular Compass Dashboard over Wi-Fi.
  =============================================================================
*/

#include <Arduino.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>

// -----------------------------------------------------------------------------
// Pin Definitions for Seeed XIAO ESP32S3 + Wio-SX1262 B2B Connector
// -----------------------------------------------------------------------------
#define LORA_LDO_EN_PIN   38   // Power / RF switch enable on Wio-SX1262
#define LORA_NSS_PIN      41   // SPI Chip Select
#define LORA_DIO1_PIN     39   // Interrupt (DIO1)
#define LORA_NRST_PIN     42   // Hardware Reset
#define LORA_BUSY_PIN     40   // Busy status line

SX1262 radio = new Module(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_NRST_PIN, LORA_BUSY_PIN);

// -----------------------------------------------------------------------------
// RF Configuration (Must match Transmitter 60 Hz settings)
// -----------------------------------------------------------------------------
const float LORA_FREQ_MHZ = 915.0;     // US915 Band
const float LORA_BW_KHZ   = 500.0;     // 500 kHz Bandwidth for 60 Hz operation
const uint8_t LORA_SF     = 7;         // Spreading Factor 7
const uint8_t LORA_CR     = 5;         // Coding Rate 4/5
const uint8_t LORA_SYNC   = RADIOLIB_SX126X_SYNC_WORD_PRIVATE;
const int8_t  LORA_PWR    = 14;        // 14 dBm
const uint16_t LORA_PREAMBLE = 6;      // 6-symbol preamble
const float   LORA_TCXO   = 1.6;       // 1.6V reference for Wio-SX1262 TCXO

// -----------------------------------------------------------------------------
// Compact 9-Byte Binary LoRa Packet (Must match Transmitter)
// -----------------------------------------------------------------------------
struct __attribute__((packed)) LoRaHeadingPacket {
  uint8_t  magic;         // 0xA5 packet identifier
  uint16_t seq;           // Rolling packet sequence counter
  uint16_t heading_x10;   // Heading * 10 (0 .. 3600 -> 0.0 .. 360.0 deg)
  int16_t  roll_x10;      // Roll * 10 (-1800 .. +1800 -> -180.0 .. +180.0 deg)
  int16_t  pitch_x10;     // Pitch * 10 (-900 .. +900 -> -90.0 .. +90.0 deg)
};

// DIO1 Receive Interrupt Flag
volatile bool receivedFlag = false;

#if defined(ESP32) || defined(ESP8266)
IRAM_ATTR
#endif
void onPacketReceived(void) {
  receivedFlag = true;
}

// -----------------------------------------------------------------------------
// Receiver State & Telemetry Statistics
// -----------------------------------------------------------------------------
float latestRawHeadingDeg = 0.0f;
float latestDisplayHeadingDeg = 0.0f;
float receiverZeroOffsetDeg = 0.0f;
float latestRollDeg  = 0.0f;
float latestPitchDeg = 0.0f;
float latestRSSI     = 0.0f;
float latestSNR      = 0.0f;
uint16_t latestSeq   = 0;
uint32_t totalRxPackets = 0;
uint32_t lostPackets    = 0;
bool hasFirstPacket     = false;
uint16_t lastSeq        = 0;

// Optional Wi-Fi Dashboard Server on Receiver (http://192.168.4.1)
const char* RX_AP_SSID = "ASME-Pitwall-RX";
WebServer server(80);

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>ASME Pitwall RX — 60 Hz LoRa Compass</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, sans-serif; background: #0b0f17; color: #f1f5f9; display: flex; justify-content: center; align-items: center; min-height: 100vh; margin: 0; padding: 16px; }
    .card { background: #131b2e; border: 1px solid #23304d; border-radius: 18px; padding: 22px; max-width: 440px; width: 100%; text-align: center; }
    h1 { font-size: 1.1rem; margin: 0 0 12px 0; }
    canvas { background: #090d16; border-radius: 50%; border: 2px solid #23304d; display: block; margin: 0 auto 12px auto; }
    .val { font-size: 2.2rem; font-weight: 800; color: #38bdf8; font-variant-numeric: tabular-nums; }
    .meta { font-size: 0.82rem; color: #94a3b8; margin-top: 6px; }
    .btns { display: flex; gap: 10px; justify-content: center; margin-bottom: 14px; }
    button { background: #1e293b; color: #f1f5f9; border: 1px solid #23304d; padding: 8px 14px; border-radius: 10px; cursor: pointer; font-weight: 600; }
  </style>
</head>
<body>
  <div class="card">
    <h1>ASME Pitwall Receiver — Live LoRa Compass</h1>
    <div class="btns">
      <button onclick="fetch('/zero')">🎯 Zero Here</button>
      <button onclick="fetch('/reset')">🧭 Compass Reset</button>
    </div>
    <canvas id="c" width="280" height="280"></canvas>
    <div class="val" id="hdg">---.-°</div>
    <div class="meta" id="meta">Waiting for 60 Hz LoRa stream...</div>
  </div>
  <script>
    const canvas = document.getElementById('c');
    const ctx = canvas.getContext('2d');
    let heading = 0, hasData = false;
    function draw() {
      const cx = 140, cy = 140, r = 118;
      ctx.clearRect(0, 0, 280, 280);
      ctx.save(); ctx.translate(cx, cy);
      for (let d = 0; d < 360; d += 10) {
        const rad = (d - 90) * Math.PI / 180;
        const len = (d % 90 === 0) ? 14 : ((d % 30 === 0) ? 9 : 5);
        ctx.beginPath();
        ctx.moveTo(Math.cos(rad)*(r-len), Math.sin(rad)*(r-len));
        ctx.lineTo(Math.cos(rad)*r, Math.sin(rad)*r);
        ctx.strokeStyle = (d % 90 === 0) ? '#f1f5f9' : '#475569';
        ctx.lineWidth = (d % 90 === 0) ? 2 : 1;
        ctx.stroke();
      }
      if (hasData) {
        ctx.save(); ctx.rotate(heading * Math.PI / 180);
        ctx.beginPath(); ctx.moveTo(0, 40); ctx.lineTo(0, -r + 32);
        ctx.strokeStyle = '#38bdf8'; ctx.lineWidth = 5; ctx.lineCap = 'round'; ctx.stroke();
        ctx.beginPath(); ctx.moveTo(0, -r + 18); ctx.lineTo(-10, -r + 38); ctx.lineTo(10, -r + 38);
        ctx.closePath(); ctx.fillStyle = '#ef4444'; ctx.fill();
        ctx.restore();
      }
      ctx.beginPath(); ctx.arc(0, 0, 5, 0, Math.PI*2); ctx.fillStyle = '#f1f5f9'; ctx.fill();
      ctx.restore();
      requestAnimationFrame(draw);
    }
    requestAnimationFrame(draw);
    setInterval(async () => {
      try {
        const res = await fetch('/state');
        const d = await res.json();
        if (d.rx > 0) {
          hasData = true; heading = d.h;
          document.getElementById('hdg').textContent = d.h.toFixed(1) + '°';
          document.getElementById('meta').textContent = `RSSI: ${d.rssi} dBm | SNR: ${d.snr} dB | RX: #${d.seq}`;
        }
      } catch (e) {}
    }, 50);
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1500);

  // 1. Enable power / RF switch to Wio-SX1262
  pinMode(LORA_LDO_EN_PIN, OUTPUT);
  digitalWrite(LORA_LDO_EN_PIN, HIGH);
  delay(50);

  // 2. Configure TCXO & Initialize SX1262 for 60 Hz (915 MHz, BW 500 kHz, SF7)
  radio.tcxoVoltage = LORA_TCXO;
  int state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC, LORA_PWR, LORA_PREAMBLE);
  if (state != RADIOLIB_ERR_NONE) {
    while (true) {
      Serial.printf("LoRa_RX_Init_Failed:%d\n", state);
      delay(1000);
    }
  }

  // 3. Attach DIO1 interrupt handler & start continuous receive mode
  radio.setDio1Action(onPacketReceived);
  radio.startReceive();

  // 4. Start optional Pitwall Wi-Fi AP Dashboard (http://192.168.4.1)
  WiFi.mode(WIFI_AP);
  WiFi.softAP(RX_AP_SSID);

  server.on("/", []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });
  server.on("/state", []() {
    char json[160];
    snprintf(json, sizeof(json),
             "{\"h\":%.1f,\"r\":%.1f,\"p\":%.1f,\"rssi\":%.1f,\"snr\":%.1f,\"seq\":%u,\"rx\":%lu}",
             latestDisplayHeadingDeg, latestRollDeg, latestPitchDeg,
             latestRSSI, latestSNR, (unsigned)latestSeq, (unsigned long)totalRxPackets);
    server.send(200, "application/json", json);
  });
  server.on("/zero", []() {
    receiverZeroOffsetDeg = latestRawHeadingDeg;
    server.send(200, "text/plain", "OK");
  });
  server.on("/reset", []() {
    receiverZeroOffsetDeg = 0.0f;
    server.send(200, "text/plain", "OK");
  });
  server.begin();
}

void loop() {
  // 1. Handle 'z' (Zero Here) and 'c' (Compass Reset) from USB Serial (`compass_plotter.html`)
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'z' || cmd == 'Z') {
      receiverZeroOffsetDeg = latestRawHeadingDeg;
    } else if (cmd == 'c' || cmd == 'C') {
      receiverZeroOffsetDeg = 0.0f;
    }
  }

  // 2. Process incoming 60 Hz LoRa packet immediately when DIO1 fires
  if (receivedFlag) {
    receivedFlag = false;

    LoRaHeadingPacket pkt;
    int state = radio.readData((uint8_t*)&pkt, sizeof(pkt));

    // Immediately re-arm receiver for the next 60 Hz packet
    radio.startReceive();

    if (state == RADIOLIB_ERR_NONE && pkt.magic == 0xA5) {
      totalRxPackets++;
      if (hasFirstPacket) {
        uint16_t gap = (uint16_t)(pkt.seq - lastSeq);
        if (gap > 1 && gap < 1000) {
          lostPackets += (gap - 1);
        }
      }
      hasFirstPacket = true;
      lastSeq = pkt.seq;
      latestSeq = pkt.seq;

      latestRawHeadingDeg = (pkt.heading_x10 % 3600) * 0.1f;
      float disp = fmodf(latestRawHeadingDeg - receiverZeroOffsetDeg + 360.0f, 360.0f);
      if (disp < 0.0f) disp += 360.0f;
      latestDisplayHeadingDeg = disp;

      latestRollDeg  = pkt.roll_x10 * 0.1f;
      latestPitchDeg = pkt.pitch_x10 * 0.1f;
      latestRSSI     = radio.getRSSI();
      latestSNR      = radio.getSNR();

      // Output to Receiver USB Serial for `compass_plotter.html` & Arduino Serial Plotter
      float rad = latestDisplayHeadingDeg * (PI / 180.0f);
      float arrowX = sinf(rad);
      float arrowY = cosf(rad);
      Serial.printf("Min:0.0,Heading:%.1f,Max:360.0,SinX:%.2f,CosY:%.2f,RSSI:%.1f,SNR:%.1f\n",
                    latestDisplayHeadingDeg, arrowX, arrowY, latestRSSI, latestSNR);
    }
  }

  // 3. Service any Wi-Fi HTTP requests cleanly
  server.handleClient();
}
