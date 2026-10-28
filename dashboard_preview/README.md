# Pitwall iPhone Web Dashboard — Interactive UI Preview

This folder contains the standalone interactive prototype for the **ASME e-HPVC Pitwall Web Dashboard** ([`iphone_dashboard_preview.html`](file:///c:/Users/Srinivas/Documents/Arduino/495%20Final%20Project/dashboard_preview/iphone_dashboard_preview.html)).

---

## How to Open

Double-click [`iphone_dashboard_preview.html`](file:///c:/Users/Srinivas/Documents/Arduino/495%20Final%20Project/dashboard_preview/iphone_dashboard_preview.html) to open it in any web browser (Chrome, Edge, Firefox, or Safari).

---

## Interactive Controls to Try

### Top Simulation Bar (Above the iPhone Frame)
* **`Mode: Fullscreen PWA` / `Mode: Safari Search Bar`:** Toggles the simulated iOS Safari bottom URL bar on and off so you can verify how the 4-tab layout dynamically adapts its height without cutting off buttons.
* **`Speed: 1x` / `Speed: 4x`:** Speeds up the simulated trike around the 0.25-mile, 10-turn track.
* **`+8ft Drift`:** Injects simulated GPS/dead-reckoning drift onto the map so you can watch the **`🏁 LAP`** button snap the position back to the Start/Finish Pin.

### Inside the 4 Bottom Tabs
1. **`🏁 Race & Map`:**
   * Click **`📍 Drop Pin`** (or simply click anywhere along the 10-turn track) to move the Start/Finish Line pin.
   * Click **`Here`** to snap the Start/Finish Pin to the trike's current live location.
   * Tap **`🏁 LAP`** to log a lap split and trigger sensor-fusion **loop closure** (snapping the trike marker back to the Start Pin and clearing accumulated drift).
   * Tap **`🔧 PIT STOP`** to log a pit stop and automatically swap the active driver to the queued next driver.
   * Tap **`📦 DELIVERY`** to record a timestamped delivery marker.
2. **`⏱️ Laps & Pit`:**
   * Tap any quick-select driver chip (`Srinivas`, `Justin`, `Alex`, `Maya`) or type a custom name and click **`Set Next`** to queue the next driver for the upcoming pit stop.
   * Review the live table of recorded laps, split times, driver swaps, and delivery stops.
3. **`🧭 Sensors`:**
   * View live simulated **Roll (lean angle)**, **Pitch**, **Compass Heading**, and **Cornering G-force** as the trike navigates the 10 turns.
   * View the **INA238** battery telemetry placeholders (Voltage, Current, Power, Wh, SoC %).
   * Adjust the **Wheel Diameter (in)** for the 50-foot straight-line calibration test.
4. **`💾 Data Dump`:**
   * View live **Packet Loss %** against the $<2\%$ deliverable target.
   * Click **`📡 Dump TX Flash Log over LoRa`** to simulate the chunked LoRa flash offload progress bar (`0% -> 100%`).
   * Click **`⬇️ TX Log (.CSV)`** or **`⬇️ Race Events (.CSV)`** to download a sample `.csv` file.
