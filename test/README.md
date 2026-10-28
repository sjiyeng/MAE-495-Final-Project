# ASME e-HPVC Telemetry System - 10 Hz LoRa Radio Test

This test verifies the hardware connection, SPI communication, and RF link between the two **Seeed Studio XIAO ESP32S3** microcontrollers and their **Wio-SX1262** LoRa modules (via the board-to-board connector) running at **10 Hz (every 100 ms)**.

---

## Folder Structure

```text
test/
├── test_transmitter/
│   └── test_transmitter.ino   # Transmits "Hello World #<n>" every 100 ms (10 Hz)
├── test_receiver/
│   └── test_receiver.ino      # Listens via DIO1 interrupt and prints 10 Hz telemetry line
└── README.md
```

---

## 1. RF Tuning for 10 Hz (Low Airtime)

To achieve **10 packets per second (100 ms per packet)**, standard long-range LoRa settings (SF9/125kHz, which take ~75 ms airtime) are too slow. 

These sketches are tuned for high update rates:
* **Frequency:** `915.0 MHz` (US915)
* **Bandwidth:** `250.0 kHz` (Doubles data rate)
* **Spreading Factor:** `7` (Fastest standard LoRa spreading factor)
* **Coding Rate:** `4/5` (Lowest overhead)
* **Packet Airtime:** **~12–15 ms** (Leaves ~85 ms idle between bursts for seamless 10 Hz throughput)

---

## 2. Pin Mapping Used (Wio-SX1262 B2B)

| Function | Pin Number | Note |
| :--- | :--- | :--- |
| **LORA_LDO_EN** | `GPIO 38` | Power / Antenna Switch Enable (set `HIGH`) |
| **NSS / CS** | `GPIO 41` | SPI Chip Select |
| **DIO1 / IRQ**| `GPIO 39` | Hardware Interrupt Line |
| **NRST / RST**| `GPIO 42` | Hardware Reset |
| **BUSY** | `GPIO 40` | Busy Line |
| **TCXO** | `1.6V` | Reference oscillator voltage for Wio-SX1262 |

---

## 3. Running the Test

1. **Upload Receiver:**
   - Open `test/test_receiver/test_receiver.ino` in Arduino IDE.
   - Upload to the receiver board and open Serial Monitor at **115200 baud**.
2. **Upload Transmitter:**
   - Open `test/test_transmitter/test_transmitter.ino` in Arduino IDE.
   - Upload to the transmitter board.
3. **Observe 10 Hz Output on Receiver Serial Monitor:**
   ```text
   -----------------------------------------------------------------
   Count | Payload               | RSSI (dBm) | SNR (dB) | Delta (ms)
   -----------------------------------------------------------------
   #1    | Hello World #1        |  -39.0 dBm |  10.2 dB |    0 ms
   #2    | Hello World #2        |  -39.5 dBm |  10.5 dB |  101 ms
   #3    | Hello World #3        |  -39.0 dBm |  10.1 dB |  100 ms
   #4    | Hello World #4        |  -38.5 dBm |  10.0 dB |   99 ms
   #5    | Hello World #5        |  -39.0 dBm |  10.4 dB |  100 ms
   ```
