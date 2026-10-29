# ASME e-HPVC — 60 Hz iPhone (ZIG SIM) $\rightarrow$ LoRa $\rightarrow$ Receiver Dashboard Test

This folder contains the end-to-end **60 Hz** wireless heading pipeline:
$$\text{iPhone 17 (ZIG SIM)} \xrightarrow{\text{60 Hz Wi-Fi UDP}} \text{Transmitter ESP32S3} \xrightarrow{\text{60 Hz 915 MHz LoRa}} \text{Receiver ESP32S3} \xrightarrow{\text{60 Hz USB Serial / Wi-Fi}} \text{2D Compass Dashboard}$$

---

## Folder Structure

```text
test_zig_lora_60hz/
├── zig_lora_transmitter/
│   └── zig_lora_transmitter.ino   # Receives 60 Hz ZIG SIM UDP & transmits 9-byte LoRa packets at 60 Hz
├── zig_lora_receiver/
│   └── zig_lora_receiver.ino      # Receives 60 Hz LoRa packets, outputs to USB Serial + hosts http://192.168.4.1
├── compass_plotter.html           # 60 Hz Web Serial 2D Circular Compass + RSSI/SNR badges
└── README.md
```

---

## How We Achieve 60 Hz (`16.6 ms` Frame Budget) Over LoRa
Standard LoRa settings (`BW = 125–250 kHz`, ASCII strings) take `15–75 ms` of airtime per packet. To hit **60 Hz**, both sketches use:
* **Bandwidth:** `500.0 kHz` (Halves symbol time)
* **Spreading Factor:** `SF7`, **Coding Rate:** `4/5`, **Preamble:** `6 symbols`
* **Compact 9-Byte Binary Struct (`LoRaHeadingPacket`):** `magic (1B) + seq (2B) + heading_x10 (2B) + roll_x10 (2B) + pitch_x10 (2B)`
* **Packet Airtime:** **`~4.8 ms`** (leaves `~11.8 ms` margin per `16.6 ms` frame)
* **Non-Blocking Interrupts (`DIO1`):** The Transmitter uses `radio.startTransmit()` so its CPU never blocks while listening for 60 Hz Wi-Fi UDP packets from the iPhone.

---

## Step-by-Step Setup

1. **Upload Transmitter (`zig_lora_transmitter.ino`):**
   * Flash `zig_lora_transmitter/zig_lora_transmitter.ino` to the **Transmitter** XIAO ESP32S3 + Wio-SX1262.
2. **Upload Receiver (`zig_lora_receiver.ino`):**
   * Flash `zig_lora_receiver/zig_lora_receiver.ino` to the **Receiver** XIAO ESP32S3 + Wio-SX1262 and leave it plugged into your computer's USB port.
3. **Start ZIG SIM on Your iPhone 17:**
   * Connect your iPhone's Wi-Fi to **`ASME-Trike-TX`**.
   * Open **ZIG SIM** (`UDP`, IP `192.168.4.1`, Port `50000`, `60` msg/sec, `QUATERNION` selected) and tap **Start**.
4. **Visualize on the Receiver (Two Ways):**
   * **Way A — Local Website over USB Serial (`compass_plotter.html`):**
     * Double-click `compass_plotter.html` in Chrome or Edge, click **`🔌 Connect USB Serial (115200)`**, and select the **Receiver** ESP32S3 COM port.
     * Watch the 2D circular compass arrow spin at **60 Hz** along with live **Rate (Hz)**, **RSSI (dBm)**, and **SNR (dB)** badges!
   * **Way B — Receiver's Built-in Wi-Fi Dashboard (`http://192.168.4.1`):**
     * Connect any device's Wi-Fi to **`ASME-Pitwall-RX`** and open `http://192.168.4.1` in a browser.
