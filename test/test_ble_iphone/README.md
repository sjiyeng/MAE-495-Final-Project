# ASME e-HPVC — Single-Board 20 Hz BLE (Bluefruit Connect) Heading Test

This folder tests the **Bluetooth Low Energy (BLE)** Nordic UART Service link between an **iPhone 17** running Adafruit's free **Bluefruit Connect** app and the **Transmitter XIAO ESP32S3**.

---

## Folder Structure

```text
test_ble_iphone/
├── README.md               # Setup & usage instructions (this file)
├── test_ble_iphone.ino     # BLE UART server & '!Q' Quaternion heading parser
└── compass_plotter.html    # Web Serial 2D Circular Compass + Rotating Arrow UI
```

---

## Step-by-Step Setup

1. **Install Bluefruit Connect on iPhone 17:**
   * Download **[Bluefruit Connect](https://apps.apple.com/us/app/bluefruit-connect/id830125974)** (free on the Apple App Store).
2. **Upload Sketch:**
   * Flash `test_ble_iphone.ino` to your **Seeed Studio XIAO ESP32S3** (`Tools -> USB CDC On Boot: "Enabled"`).
3. **Stream Quaternion Data:**
   * Open **Bluefruit Connect**, connect to **`ASME-Trike-TX`**, tap **Controller**, and toggle **ON** **`Quaternion`** (and optionally **`Location`** if you want it to continue streaming while the iPhone screen is locked).
4. **Visualize:**
   * Close the Arduino Serial Monitor, open `compass_plotter.html` in **Chrome** or **Edge**, and click **`🔌 Connect USB Serial (115200)`**.
