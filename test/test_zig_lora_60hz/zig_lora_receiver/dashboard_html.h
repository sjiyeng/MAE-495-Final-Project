/*
  =============================================================================
  dashboard_html.h — 2-Tab HTML Webpage (Compass + Connectivity & Diagnostics)
  Matches `compass_plotter.html` in the parent folder.
  =============================================================================
*/

#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>ASME e-HPVC — 60 Hz LoRa Compass & Diagnostics</title>
  <style>
    :root {
      --bg: #0b0f17;
      --card: #131b2e;
      --card-alt: #18233c;
      --border: #23304d;
      --text: #f1f5f9;
      --muted: #94a3b8;
      --accent: #38bdf8;
      --red: #ef4444;
      --amber: #f59e0b;
      --green: #22c55e;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      background: var(--bg);
      color: var(--text);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 16px;
    }
    .card {
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 18px;
      padding: 20px;
      max-width: 540px;
      width: 100%;
      box-shadow: 0 20px 50px rgba(0,0,0,0.5);
    }
    .header {
      text-align: center;
      margin-bottom: 14px;
    }
    h1 {
      font-size: 1.15rem;
      font-weight: 700;
      letter-spacing: 0.02em;
      margin-bottom: 4px;
    }
    .subtitle {
      font-size: 0.78rem;
      color: var(--muted);
    }
    .tabs {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px;
      background: #090d16;
      padding: 5px;
      border-radius: 12px;
      border: 1px solid var(--border);
      margin-bottom: 16px;
    }
    .tab-btn {
      background: transparent;
      color: var(--muted);
      border: none;
      padding: 9px 12px;
      border-radius: 8px;
      font-size: 0.85rem;
      font-weight: 700;
      cursor: pointer;
      transition: all 0.15s ease;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 6px;
    }
    .tab-btn.active {
      background: var(--card-alt);
      color: var(--text);
      box-shadow: 0 2px 8px rgba(0,0,0,0.3);
      border: 1px solid var(--border);
    }
    .tab-panel { display: none; }
    .tab-panel.active { display: block; }
    .controls {
      display: flex;
      gap: 8px;
      justify-content: center;
      flex-wrap: wrap;
      margin-bottom: 14px;
    }
    button.action-btn {
      background: #1e293b;
      color: var(--text);
      border: 1px solid var(--border);
      padding: 8px 13px;
      border-radius: 10px;
      font-size: 0.82rem;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.15s ease;
    }
    button.action-btn:hover { border-color: var(--accent); }
    button.action-btn.primary {
      background: var(--accent);
      color: #090d16;
      border-color: var(--accent);
    }
    button.action-btn.connected {
      background: var(--green);
      color: #090d16;
      border-color: var(--green);
    }
    canvas {
      display: block;
      margin: 0 auto 12px auto;
      background: #090d16;
      border-radius: 50%;
      border: 2px solid var(--border);
    }
    .readout {
      font-size: 2.2rem;
      font-weight: 800;
      font-variant-numeric: tabular-nums;
      color: var(--accent);
      text-align: center;
      margin-bottom: 4px;
    }
    .rf-badges {
      display: flex;
      justify-content: center;
      flex-wrap: wrap;
      gap: 8px;
      margin: 8px 0 10px 0;
      font-size: 0.76rem;
    }
    .badge {
      background: #1e293b;
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 4px 9px;
      font-variant-numeric: tabular-nums;
      display: inline-flex;
      align-items: center;
      gap: 5px;
    }
    .dot {
      width: 8px;
      height: 8px;
      border-radius: 50%;
      background: var(--muted);
      display: inline-block;
    }
    .dot.ok { background: var(--green); box-shadow: 0 0 8px var(--green); }
    .dot.warn { background: var(--amber); box-shadow: 0 0 8px var(--amber); }
    .dot.err { background: var(--red); box-shadow: 0 0 8px var(--red); }
    .pipeline {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 6px;
      margin-bottom: 14px;
    }
    .pipe-node {
      background: #090d16;
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 8px 6px;
      text-align: center;
      font-size: 0.72rem;
    }
    .pipe-node.ok { border-color: var(--green); }
    .pipe-node.warn { border-color: var(--amber); }
    .pipe-node.err { border-color: var(--red); }
    .pipe-title { font-weight: 700; margin-bottom: 3px; color: var(--text); }
    .pipe-sub { font-size: 0.68rem; color: var(--muted); }
    .diag-section {
      background: #090d16;
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 12px 14px;
      margin-bottom: 10px;
    }
    .diag-section-title {
      font-size: 0.76rem;
      font-weight: 700;
      text-transform: uppercase;
      letter-spacing: 0.05em;
      color: var(--accent);
      margin-bottom: 8px;
    }
    .diag-row {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 5px 0;
      border-bottom: 1px solid rgba(35, 48, 77, 0.5);
      font-size: 0.82rem;
    }
    .diag-row:last-child { border-bottom: none; }
    .diag-label { color: var(--muted); }
    .diag-val { font-weight: 700; font-variant-numeric: tabular-nums; }
    .pill {
      padding: 2px 8px;
      border-radius: 6px;
      font-size: 0.74rem;
      font-weight: 700;
    }
    .pill.ok { background: rgba(34, 197, 94, 0.18); color: #4ade80; border: 1px solid rgba(34, 197, 94, 0.4); }
    .pill.warn { background: rgba(245, 158, 11, 0.18); color: #fbbf24; border: 1px solid rgba(245, 158, 11, 0.4); }
    .pill.err { background: rgba(239, 68, 68, 0.18); color: #f87171; border: 1px solid rgba(239, 68, 68, 0.4); }
    .meter-wrap {
      width: 100%;
      height: 6px;
      background: #1e293b;
      border-radius: 4px;
      overflow: hidden;
      margin-top: 4px;
    }
    .meter-fill {
      height: 100%;
      width: 0%;
      background: var(--green);
      transition: width 0.2s ease, background 0.2s ease;
    }
    .status {
      font-size: 0.78rem;
      color: var(--muted);
      text-align: center;
      margin-top: 8px;
    }
  </style>
</head>
<body>
  <div class="card">
    <div class="header">
      <h1>ASME e-HPVC — 60 Hz LoRa Telemetry & Compass</h1>
      <p class="subtitle" id="connSubtitle">Wi-Fi: Join <b>ASME-Pitwall-RX</b> (`http://192.168.4.1`) or connect via USB Serial</p>
    </div>

    <div class="tabs">
      <button class="tab-btn active" id="tabCompassBtn" onclick="switchTab('compass')">
        🧭 Live Compass
      </button>
      <button class="tab-btn" id="tabDiagBtn" onclick="switchTab('diag')">
        <span class="dot" id="diagTabDot"></span> 📡 Connectivity & Diagnostics
      </button>
    </div>

    <div class="controls">
      <button id="zeroBtn" class="action-btn">🎯 Zero Here ('z')</button>
      <button id="compassBtn" class="action-btn">🧭 Compass Reset ('c')</button>
      <button id="resetLossBtn" class="action-btn">🔄 Reset Loss ('r')</button>
      <button id="connectBtn" class="action-btn primary">🔌 USB Serial (Optional)</button>
    </div>

    <!-- TAB 1: COMPASS -->
    <div class="tab-panel active" id="panelCompass">
      <canvas id="compassCanvas" width="300" height="300"></canvas>
      <div class="readout" id="headingValue">---.-°</div>
      <div class="rf-badges">
        <span class="badge"><span class="dot" id="phoneDot"></span> <span id="phoneBadge">Phone: --</span></span>
        <span class="badge"><span class="dot" id="streamDot"></span> <span id="streamBadge">ZIG: -- Hz</span></span>
        <span class="badge" id="rssiBadge">LoRa: -- dBm</span>
        <span class="badge" id="hzBadge">RX: -- Hz</span>
      </div>
    </div>

    <!-- TAB 2: CONNECTIVITY & DIAGNOSTICS -->
    <div class="tab-panel" id="panelDiag">
      <div class="pipeline">
        <div class="pipe-node" id="pipePhone">
          <div class="pipe-title">📱 iPhone 17</div>
          <div class="pipe-sub" id="pipePhoneSub">Waiting</div>
        </div>
        <div class="pipe-node" id="pipeZig">
          <div class="pipe-title">📤 ZIG UDP</div>
          <div class="pipe-sub" id="pipeZigSub">0 Hz</div>
        </div>
        <div class="pipe-node" id="pipeLora">
          <div class="pipe-title">📻 915 MHz</div>
          <div class="pipe-sub" id="pipeLoraSub">No Link</div>
        </div>
        <div class="pipe-node" id="pipeRx">
          <div class="pipe-title">📥 Pitwall RX</div>
          <div class="pipe-sub" id="pipeRxSub">Ready</div>
        </div>
      </div>

      <div class="diag-section">
        <div class="diag-section-title">1. Trike iPhone 17 ↔ Transmitter Link</div>
        <div class="diag-row">
          <span class="diag-label">iPhone Wi-Fi (`ASME-Trike-TX`)</span>
          <span class="pill err" id="dPhoneConn">DISCONNECTED</span>
        </div>
        <div class="diag-row">
          <span class="diag-label">Streaming Data (ZIG SIM UDP)</span>
          <span class="pill err" id="dZigStream">NOT STREAMING</span>
        </div>
        <div class="diag-row">
          <span class="diag-label">ZIG SIM Update Rate</span>
          <span class="diag-val" id="dZigHz">0 Hz</span>
        </div>
        <div class="diag-row" style="display:block;">
          <div style="display:flex; justify-content:space-between;">
            <span class="diag-label">iPhone Wi-Fi Signal Strength</span>
            <span class="diag-val" id="dPhoneRssi">-- dBm</span>
          </div>
          <div class="meter-wrap"><div class="meter-fill" id="dPhoneMeter"></div></div>
        </div>
      </div>

      <div class="diag-section">
        <div class="diag-section-title">2. 915 MHz LoRa Radio Link (500 kHz / SF7)</div>
        <div class="diag-row">
          <span class="diag-label">LoRa Radio Link Status</span>
          <span class="pill err" id="dLoraStatus">WAITING FOR TX</span>
        </div>
        <div class="diag-row" style="display:block;">
          <div style="display:flex; justify-content:space-between;">
            <span class="diag-label">LoRa Signal Strength (RSSI)</span>
            <span class="diag-val" id="dLoraRssi">-- dBm</span>
          </div>
          <div class="meter-wrap"><div class="meter-fill" id="dLoraMeter"></div></div>
        </div>
        <div class="diag-row">
          <span class="diag-label">LoRa Signal-to-Noise (SNR)</span>
          <span class="diag-val" id="dLoraSnr">-- dB</span>
        </div>
        <div class="diag-row">
          <span class="diag-label">LoRa Receive Rate</span>
          <span class="diag-val" id="dLoraHz">0 Hz</span>
        </div>
        <div class="diag-row">
          <span class="diag-label">Packet Loss (Target &lt; 2.0%)</span>
          <span style="display:flex; align-items:center; gap:8px;">
            <span class="diag-val" id="dLoss">0.00% (0 lost / 0 rx)</span>
            <button onclick="sendCommand('r')" class="action-btn" style="padding:3px 8px; font-size:0.72rem;">🔄 Reset</button>
          </span>
        </div>
        <div class="diag-row">
          <span class="diag-label">Last LoRa Packet Age</span>
          <span class="diag-val" id="dPacketAge">-- ms</span>
        </div>
      </div>

      <div class="diag-section">
        <div class="diag-section-title">3. Attitude & Orientation Lock</div>
        <div class="diag-row">
          <span class="diag-label">3D Mounting Reference Pose</span>
          <span class="pill warn" id="dPoseLock">WAITING FOR QUAT</span>
        </div>
        <div class="diag-row">
          <span class="diag-label">Heading / Roll / Pitch</span>
          <span class="diag-val" id="dAttitude">---.-° / --.-° / --.-°</span>
        </div>
        <div class="diag-row">
          <span class="diag-label">Dashboard Connection Source</span>
          <span class="diag-val" id="dSource">Wi-Fi (`192.168.4.1`)</span>
        </div>
      </div>
    </div>

    <div class="status" id="statusText">Connecting to Receiver Wi-Fi (`http://192.168.4.1/state`)...</div>
  </div>

  <script>
    function switchTab(tab) {
      document.getElementById('tabCompassBtn').classList.toggle('active', tab === 'compass');
      document.getElementById('tabDiagBtn').classList.toggle('active', tab === 'diag');
      document.getElementById('panelCompass').classList.toggle('active', tab === 'compass');
      document.getElementById('panelDiag').classList.toggle('active', tab === 'diag');
    }

    const connectBtn = document.getElementById('connectBtn');
    const zeroBtn = document.getElementById('zeroBtn');
    const compassBtn = document.getElementById('compassBtn');
    const headingValue = document.getElementById('headingValue');
    const statusText = document.getElementById('statusText');
    const canvas = document.getElementById('compassCanvas');
    const ctx = canvas.getContext('2d');

    const state = {
      heading: 0,
      roll: 0,
      pitch: 0,
      phoneConnected: false,
      zigStreaming: false,
      hasQuat: false,
      zigHz: 0,
      phoneRssi: -127,
      loraOnline: false,
      loraRssi: null,
      loraSnr: null,
      loraRxHz: 0,
      seq: 0,
      totalRx: 0,
      lostPackets: 0,
      lastRxAgeMs: 9999,
      source: 'None'
    };

    let port = null;
    let writer = null;
    let serialActive = false;
    let serialPacketsThisSec = 0;
    let lastSerialPacketTime = 0;

    setInterval(() => {
      if (serialActive) {
        state.loraRxHz = serialPacketsThisSec;
        serialPacketsThisSec = 0;
        state.lastRxAgeMs = Date.now() - lastSerialPacketTime;
        state.loraOnline = (state.lastRxAgeMs < 1000);
      }
    }, 1000);

    function rssiToPercent(rssi) {
      if (rssi === null || rssi <= -120) return 0;
      return Math.max(0, Math.min(100, Math.round(((rssi + 110) / 70) * 100)));
    }

    function rssiQualityLabel(rssi) {
      if (rssi === null || rssi <= -120) return 'No Signal';
      if (rssi >= -60) return 'Excellent';
      if (rssi >= -78) return 'Good';
      if (rssi >= -92) return 'Fair';
      return 'Weak';
    }

    function updateDiagnosticsUI() {
      const diagDot = document.getElementById('diagTabDot');
      const phoneDot = document.getElementById('phoneDot');
      const streamDot = document.getElementById('streamDot');

      phoneDot.className = 'dot ' + (state.phoneConnected ? 'ok' : 'err');
      streamDot.className = 'dot ' + (state.zigStreaming ? 'ok' : (state.phoneConnected ? 'warn' : 'err'));
      diagDot.className = 'dot ' + ((state.loraOnline && state.zigStreaming) ? 'ok' : (state.loraOnline ? 'warn' : 'err'));

      document.getElementById('phoneBadge').textContent = state.phoneConnected ? 'Phone: Connected' : 'Phone: Disconnected';
      document.getElementById('streamBadge').textContent = state.zigStreaming ? `ZIG: ${state.zigHz} Hz` : 'ZIG: Paused';
      document.getElementById('rssiBadge').textContent = (state.loraRssi !== null && state.loraOnline) ? `LoRa: ${state.loraRssi.toFixed(0)} dBm` : 'LoRa: -- dBm';
      document.getElementById('hzBadge').textContent = `RX: ${state.loraRxHz} Hz`;

      const pPhone = document.getElementById('pipePhone');
      pPhone.className = 'pipe-node ' + (state.phoneConnected ? 'ok' : 'err');
      document.getElementById('pipePhoneSub').textContent = state.phoneConnected ? 'Wi-Fi OK' : 'No Wi-Fi';

      const pZig = document.getElementById('pipeZig');
      pZig.className = 'pipe-node ' + (state.zigStreaming ? 'ok' : (state.phoneConnected ? 'warn' : 'err'));
      document.getElementById('pipeZigSub').textContent = state.zigStreaming ? `${state.zigHz} Hz UDP` : 'Stopped';

      const pLora = document.getElementById('pipeLora');
      pLora.className = 'pipe-node ' + (state.loraOnline ? 'ok' : 'err');
      document.getElementById('pipeLoraSub').textContent = (state.loraOnline && state.loraRssi !== null) ? `${state.loraRssi.toFixed(0)} dBm` : 'Offline';

      const pRx = document.getElementById('pipeRx');
      pRx.className = 'pipe-node ' + (state.source !== 'None' ? 'ok' : 'warn');
      document.getElementById('pipeRxSub').textContent = state.source !== 'None' ? state.source : 'Waiting';

      const dPhoneConn = document.getElementById('dPhoneConn');
      dPhoneConn.textContent = state.phoneConnected ? 'CONNECTED' : 'DISCONNECTED';
      dPhoneConn.className = 'pill ' + (state.phoneConnected ? 'ok' : 'err');

      const dZigStream = document.getElementById('dZigStream');
      dZigStream.textContent = state.zigStreaming ? 'STREAMING ACTIVE' : (state.phoneConnected ? 'PAUSED (TAP START)' : 'NOT STREAMING');
      dZigStream.className = 'pill ' + (state.zigStreaming ? 'ok' : (state.phoneConnected ? 'warn' : 'err'));

      document.getElementById('dZigHz').textContent = `${state.zigHz} Hz`;

      const phonePct = state.phoneConnected ? rssiToPercent(state.phoneRssi) : 0;
      document.getElementById('dPhoneRssi').textContent = state.phoneConnected
        ? `${state.phoneRssi} dBm (${rssiQualityLabel(state.phoneRssi)})`
        : '-- dBm';
      const phoneMeter = document.getElementById('dPhoneMeter');
      phoneMeter.style.width = phonePct + '%';
      phoneMeter.style.background = phonePct > 50 ? 'var(--green)' : (phonePct > 25 ? 'var(--amber)' : 'var(--red)');

      const dLoraStatus = document.getElementById('dLoraStatus');
      dLoraStatus.textContent = state.loraOnline ? 'ONLINE (RECEIVING)' : 'NO LORA SIGNAL';
      dLoraStatus.className = 'pill ' + (state.loraOnline ? 'ok' : 'err');

      const loraPct = state.loraOnline ? rssiToPercent(state.loraRssi) : 0;
      document.getElementById('dLoraRssi').textContent = (state.loraOnline && state.loraRssi !== null)
        ? `${state.loraRssi.toFixed(1)} dBm (${rssiQualityLabel(state.loraRssi)})`
        : '-- dBm';
      const loraMeter = document.getElementById('dLoraMeter');
      loraMeter.style.width = loraPct + '%';
      loraMeter.style.background = loraPct > 50 ? 'var(--green)' : (loraPct > 25 ? 'var(--amber)' : 'var(--red)');

      document.getElementById('dLoraSnr').textContent = (state.loraOnline && state.loraSnr !== null)
        ? `${state.loraSnr > 0 ? '+' : ''}${state.loraSnr.toFixed(1)} dB`
        : '-- dB';
      document.getElementById('dLoraHz').textContent = `${state.loraRxHz} Hz`;

      const totalExpected = state.totalRx + state.lostPackets;
      const lossPct = totalExpected > 0 ? ((state.lostPackets / totalExpected) * 100).toFixed(2) : '0.00';
      document.getElementById('dLoss').textContent = `${lossPct}% (${state.lostPackets} lost / ${state.totalRx} rx)`;
      document.getElementById('dPacketAge').textContent = state.loraOnline ? `${state.lastRxAgeMs} ms ago` : 'Offline';

      const dPoseLock = document.getElementById('dPoseLock');
      dPoseLock.textContent = state.hasQuat ? '3D POSE LOCKED' : 'WAITING FOR QUAT';
      dPoseLock.className = 'pill ' + (state.hasQuat ? 'ok' : 'warn');

      document.getElementById('dAttitude').textContent = state.hasQuat
        ? `${state.heading.toFixed(1)}° / ${state.roll.toFixed(1)}° / ${state.pitch.toFixed(1)}°`
        : '---.-° / --.-° / --.-°';
      document.getElementById('dSource').textContent = state.source;
    }

    function drawCompass(headingDeg) {
      const w = canvas.width;
      const h = canvas.height;
      const cx = w / 2;
      const cy = h / 2;
      const r = cx - 24;

      ctx.clearRect(0, 0, w, h);
      ctx.save();
      ctx.translate(cx, cy);

      for (let deg = 0; deg < 360; deg += 5) {
        const rad = (deg - 90) * Math.PI / 180;
        const isMajor = (deg % 30 === 0);
        const isCardinal = (deg % 90 === 0);
        const innerR = isCardinal ? r - 15 : (isMajor ? r - 10 : r - 5);

        ctx.beginPath();
        ctx.moveTo(Math.cos(rad) * innerR, Math.sin(rad) * innerR);
        ctx.lineTo(Math.cos(rad) * r, Math.sin(rad) * r);
        ctx.strokeStyle = isCardinal ? '#f1f5f9' : (isMajor ? '#64748b' : '#334155');
        ctx.lineWidth = isCardinal ? 2.5 : (isMajor ? 1.5 : 1);
        ctx.stroke();
      }

      const labels = [
        { text: 'N (0°)', angle: 0, color: '#ef4444' },
        { text: 'E (90°)', angle: 90, color: '#94a3b8' },
        { text: 'S (180°)', angle: 180, color: '#94a3b8' },
        { text: 'W (270°)', angle: 270, color: '#94a3b8' }
      ];
      ctx.font = 'bold 12px -apple-system, sans-serif';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      labels.forEach(lbl => {
        const rad = (lbl.angle - 90) * Math.PI / 180;
        const labelR = r - 28;
        ctx.fillStyle = lbl.color;
        ctx.fillText(lbl.text, Math.cos(rad) * labelR, Math.sin(rad) * labelR);
      });

      ctx.beginPath();
      ctx.moveTo(-r + 44, 0); ctx.lineTo(r - 44, 0);
      ctx.moveTo(0, -r + 44); ctx.lineTo(0, r - 44);
      ctx.strokeStyle = 'rgba(148, 163, 184, 0.15)';
      ctx.lineWidth = 1;
      ctx.stroke();

      if (state.hasQuat || state.loraOnline) {
        const arrowAngle = headingDeg * Math.PI / 180;
        ctx.save();
        ctx.rotate(arrowAngle);

        const tipY = -(r - 42);
        const tailY = (r - 60);

        ctx.beginPath();
        ctx.moveTo(0, 0);
        ctx.lineTo(0, tailY);
        ctx.strokeStyle = '#475569';
        ctx.lineWidth = 4;
        ctx.lineCap = 'round';
        ctx.stroke();

        ctx.beginPath();
        ctx.moveTo(0, 0);
        ctx.lineTo(0, tipY + 14);
        ctx.strokeStyle = '#38bdf8';
        ctx.lineWidth = 5;
        ctx.lineCap = 'round';
        ctx.stroke();

        ctx.beginPath();
        ctx.moveTo(0, tipY);
        ctx.lineTo(-11, tipY + 22);
        ctx.lineTo(0, tipY + 16);
        ctx.lineTo(11, tipY + 22);
        ctx.closePath();
        ctx.fillStyle = '#ef4444';
        ctx.fill();

        ctx.restore();
      }

      ctx.beginPath();
      ctx.arc(0, 0, 6, 0, Math.PI * 2);
      ctx.fillStyle = '#f1f5f9';
      ctx.fill();

      ctx.restore();
    }

    function renderLoop() {
      drawCompass(state.heading);
      if (state.hasQuat || state.loraOnline) {
        headingValue.textContent = state.heading.toFixed(1) + '°';
      }
      updateDiagnosticsUI();
      requestAnimationFrame(renderLoop);
    }
    requestAnimationFrame(renderLoop);

    const WIFI_BASE = (window.location.hostname === '192.168.4.1') ? '' : 'http://192.168.4.1';

    async function sendCommand(cmd) {
      if (cmd === 'r') {
        state.totalRx = 0;
        state.lostPackets = 0;
      }
      if (writer) {
        const data = new TextEncoder().encode(cmd);
        await writer.write(data);
      } else {
        try {
          const path = (cmd === 'z') ? '/zero' : ((cmd === 'r') ? '/clear_stats' : '/reset');
          await fetch(WIFI_BASE + path);
        } catch (e) {}
      }
    }

    zeroBtn.addEventListener('click', () => sendCommand('z'));
    compassBtn.addEventListener('click', () => sendCommand('c'));
    document.getElementById('resetLossBtn').addEventListener('click', () => sendCommand('r'));

    async function pollWifiLoop() {
      while (!serialActive) {
        try {
          const controller = new AbortController();
          const timeout = setTimeout(() => controller.abort(), 600);
          const res = await fetch(WIFI_BASE + '/state', { signal: controller.signal, cache: 'no-store' });
          clearTimeout(timeout);
          if (res.ok) {
            const d = await res.json();
            state.heading        = d.h ?? 0;
            state.roll           = d.r ?? 0;
            state.pitch          = d.p ?? 0;
            state.phoneConnected = !!d.pc;
            state.zigStreaming   = !!d.zs;
            state.hasQuat        = !!d.hq;
            state.zigHz          = d.zhz ?? 0;
            state.phoneRssi      = d.prssi ?? -127;
            state.loraOnline     = (d.age < 1000) && (d.rx > 0);
            state.loraRssi       = d.rssi ?? null;
            state.loraSnr        = d.snr ?? null;
            state.loraRxHz       = d.lhz ?? 0;
            state.seq            = d.seq ?? 0;
            state.totalRx        = d.rx ?? 0;
            state.lostPackets    = d.lost ?? 0;
            state.lastRxAgeMs    = d.age ?? 9999;
            state.source         = 'Wi-Fi (ASME-Pitwall-RX)';
            statusText.textContent = state.loraOnline
              ? `Connected over Wi-Fi — Receiving LoRa at ${state.loraRxHz} Hz`
              : `Connected to Receiver Wi-Fi — Waiting for Transmitter LoRa packets...`;
          }
        } catch (e) {
          if (!serialActive) {
            state.source = 'None';
            statusText.textContent = 'Join Wi-Fi "ASME-Pitwall-RX" (http://192.168.4.1) or click USB Serial';
          }
        }
        await new Promise(r => setTimeout(r, 25));
      }
    }
    pollWifiLoop();

    connectBtn.addEventListener('click', async () => {
      if (!('serial' in navigator)) {
        alert('Web Serial API requires Google Chrome or Microsoft Edge.');
        return;
      }
      try {
        port = await navigator.serial.requestPort();
        await port.open({ baudRate: 115200 });

        writer = port.writable.getWriter();
        serialActive = true;
        connectBtn.textContent = '✅ USB Serial Active';
        connectBtn.classList.add('connected');
        state.source = 'USB Serial (115200)';

        const textDecoder = new TextDecoderStream();
        port.readable.pipeTo(textDecoder.writable);
        const reader = textDecoder.readable.getReader();

        let buffer = '';
        while (true) {
          const { value, done } = await reader.read();
          if (done) break;
          buffer += value;
          let lines = buffer.split(/\r?\n/);
          buffer = lines.pop();

          for (const line of lines) {
            const hMatch = line.match(/Heading:\s*([0-9.]+)/i);
            if (hMatch) {
              state.heading = parseFloat(hMatch[1]);
              state.hasQuat = true;
              serialPacketsThisSec++;
              lastSerialPacketTime = Date.now();
              state.loraOnline = true;

              const rMatch = line.match(/RSSI:\s*([-0-9.]+)/i);
              if (rMatch) state.loraRssi = parseFloat(rMatch[1]);

              const sMatch = line.match(/SNR:\s*([-0-9.]+)/i);
              if (sMatch) state.loraSnr = parseFloat(sMatch[1]);

              const rollM = line.match(/Roll:\s*([-0-9.]+)/i);
              if (rollM) state.roll = parseFloat(rollM[1]);

              const pitchM = line.match(/Pitch:\s*([-0-9.]+)/i);
              if (pitchM) state.pitch = parseFloat(pitchM[1]);

              const pcM = line.match(/PhoneConn:\s*([01])/i);
              if (pcM) state.phoneConnected = (pcM[1] === '1');

              const stM = line.match(/Streaming:\s*([01])/i);
              if (stM) state.zigStreaming = (stM[1] === '1');

              const zhM = line.match(/ZigHz:\s*([0-9]+)/i);
              if (zhM) state.zigHz = parseInt(zhM[1], 10);

              const prM = line.match(/PhoneRSSI:\s*([-0-9]+)/i);
              if (prM) state.phoneRssi = parseInt(prM[1], 10);

              const rxM = line.match(/Rx:\s*([0-9]+)/i);
              if (rxM) state.totalRx = parseInt(rxM[1], 10);

              const lostM = line.match(/Lost:\s*([0-9]+)/i);
              if (lostM) state.lostPackets = parseInt(lostM[1], 10);

              statusText.textContent = `Streaming via USB Serial at ${state.loraRxHz} Hz`;
            }
          }
        }
      } catch (err) {
        statusText.textContent = 'USB Serial error: ' + err.message;
      }
    });
  </script>
</body>
</html>
)rawliteral";
