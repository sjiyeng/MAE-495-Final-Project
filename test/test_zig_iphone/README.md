# ASME e-HPVC — Single-Board 60 Hz ZIG SIM (iPhone 17) Heading Test

This folder tests the **60 Hz Wi-Fi UDP** link between an **iPhone 17** running the free **ZIG SIM** app and a single **Transmitter XIAO ESP32S3**, outputting orientation-independent `0–360°` heading at **60 Hz** to `compass_plotter.html` (or the Arduino IDE Serial Plotter).

---

## Folder Structure

```text
test_zig_iphone/
├── README.md               # Setup & usage instructions (this file)
├── test_zig_iphone.ino     # 60 Hz Wi-Fi UDP ZIG SIM receiver & quaternion heading parser
└── compass_plotter.html    # 60 Hz Web Serial 2D Circular Compass + Rotating Arrow UI
```

---

## Step-by-Step Setup

1. **Install ZIG SIM on iPhone 17:**
   * Download **[ZIG SIM](https://apps.apple.com/us/app/zig-sim/id1112909974)** (free on the Apple App Store).
2. **Upload Sketch:**
   * Flash `test_zig_iphone.ino` to your **Seeed Studio XIAO ESP32S3** (`Tools -> USB CDC On Boot: "Enabled"`). Ensure the 2.4 GHz Wi-Fi antenna is attached.
3. **Connect iPhone Wi-Fi:**
   * Connect your iPhone 17 to the **`ASME-Trike-TX`** Wi-Fi network (open network, no password).
4. **Configure ZIG SIM:**
   * **Settings Tab:** `DATA DESTINATION = OTHER APP`, `PROTOCOL = UDP`, `IP ADDRESS = 192.168.4.1`, `PORT NUMBER = 50000`, `MESSAGE FORMAT = JSON` (or `OSC`), `MESSAGE RATE PER SEC = 60`.
   * **Sensor Tab:** Enable **`QUATERNION`** only.
   * **Start Tab:** Tap **`Start`**.
5. **Open the 60 Hz 2D Compass Plotter:**
   * Close the Arduino Serial Monitor, open `compass_plotter.html` in **Chrome** or **Edge**, and click **`🔌 Connect USB Serial (115200)`**.
   * Use **`🎯 Zero Here ('z')`** to lock the current heading as `0.0°` or **`🧭 Compass Reset ('c')`** to reset to compass heading.
