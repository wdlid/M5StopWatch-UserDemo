/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

namespace ota {

inline constexpr const char OTA_HTML[] = R"rawliteral(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>M5Stack StopWatch - OTA Firmware Update</title>
  <style>
    :root {
      --bg: #0f172a;
      --card-bg: #1e293b;
      --card-border: #334155;
      --text: #f8fafc;
      --text-muted: #94a3b8;
      --primary: #4ad78c;
      --primary-hover: #38bdf8;
      --primary-glow: rgba(74, 215, 140, 0.25);
      --danger: #f43f5e;
      --danger-bg: rgba(244, 63, 94, 0.15);
      --success: #10b981;
      --success-bg: rgba(16, 185, 129, 0.15);
      --progress-bg: #0f172a;
    }
    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
    }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      background: var(--bg);
      color: var(--text);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 20px;
    }
    .container {
      width: 100%;
      max-width: 580px;
      display: flex;
      flex-direction: column;
      gap: 20px;
    }
    .header {
      text-align: center;
    }
    .header h1 {
      font-size: 24px;
      font-weight: 700;
      letter-spacing: -0.5px;
      margin-bottom: 6px;
      color: var(--text);
    }
    .header .subtitle {
      font-size: 14px;
      color: var(--text-muted);
    }
    .card {
      background: var(--card-bg);
      border: 1px solid var(--card-border);
      border-radius: 16px;
      padding: 24px;
      box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.5);
    }
    .info-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 14px;
      margin-bottom: 6px;
    }
    .info-item {
      background: rgba(15, 23, 42, 0.6);
      border: 1px solid var(--card-border);
      padding: 12px 14px;
      border-radius: 10px;
    }
    .info-label {
      font-size: 11px;
      text-transform: uppercase;
      letter-spacing: 0.5px;
      color: var(--text-muted);
      margin-bottom: 4px;
    }
    .info-value {
      font-size: 15px;
      font-weight: 600;
      color: var(--primary);
    }
    .drop-zone {
      border: 2px dashed var(--card-border);
      border-radius: 12px;
      padding: 32px 20px;
      text-align: center;
      cursor: pointer;
      transition: all 0.2s ease;
      background: rgba(15, 23, 42, 0.4);
    }
    .drop-zone.dragover {
      border-color: var(--primary);
      background: var(--primary-glow);
    }
    .drop-zone svg {
      width: 48px;
      height: 48px;
      fill: none;
      stroke: var(--text-muted);
      stroke-width: 2;
      stroke-linecap: round;
      stroke-linejoin: round;
      margin-bottom: 12px;
      transition: stroke 0.2s;
    }
    .drop-zone:hover svg {
      stroke: var(--primary);
    }
    .drop-zone-text {
      font-size: 14px;
      font-weight: 500;
      color: var(--text);
      margin-bottom: 6px;
    }
    .drop-zone-hint {
      font-size: 12px;
      color: var(--text-muted);
    }
    .file-input {
      display: none;
    }
    .file-details {
      display: none;
      margin-top: 16px;
      padding: 12px 14px;
      background: rgba(15, 23, 42, 0.6);
      border: 1px solid var(--card-border);
      border-radius: 8px;
      justify-content: space-between;
      align-items: center;
      font-size: 13px;
    }
    .file-details.active {
      display: flex;
    }
    .file-name {
      font-weight: 600;
      color: var(--text);
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
      max-width: 320px;
    }
    .file-size {
      color: var(--text-muted);
      font-size: 12px;
    }
    .btn {
      width: 100%;
      margin-top: 20px;
      padding: 14px;
      border: none;
      border-radius: 10px;
      font-size: 15px;
      font-weight: 600;
      cursor: pointer;
      background: var(--primary);
      color: #052e16;
      transition: all 0.2s ease;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 8px;
    }
    .btn:hover:not(:disabled) {
      background: var(--primary-hover);
      color: #082f49;
      box-shadow: 0 0 15px var(--primary-glow);
    }
    .btn:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
    .progress-section {
      display: none;
      margin-top: 20px;
    }
    .progress-section.active {
      display: block;
    }
    .progress-bar-container {
      width: 100%;
      height: 14px;
      background: var(--progress-bg);
      border-radius: 7px;
      overflow: hidden;
      border: 1px solid var(--card-border);
    }
    .progress-bar {
      height: 100%;
      width: 0%;
      background: linear-gradient(90deg, var(--primary) 0%, var(--primary-hover) 100%);
      transition: width 0.15s ease;
    }
    .progress-meta {
      display: flex;
      justify-content: space-between;
      font-size: 12px;
      color: var(--text-muted);
      margin-top: 8px;
    }
    .alert {
      display: none;
      padding: 14px 16px;
      border-radius: 10px;
      font-size: 13px;
      line-height: 1.5;
      margin-top: 16px;
    }
    .alert.active {
      display: block;
    }
    .alert-success {
      background: var(--success-bg);
      border: 1px solid var(--success);
      color: #34d399;
    }
    .alert-danger {
      background: var(--danger-bg);
      border: 1px solid var(--danger);
      color: #fb7185;
    }
    .footer {
      text-align: center;
      font-size: 12px;
      color: var(--text-muted);
    }
    .footer a {
      color: var(--primary);
      text-decoration: none;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>M5Stack StopWatch</h1>
      <div class="subtitle">Over-The-Air (OTA) Firmware Upgrade</div>
    </div>

    <div class="card">
      <div class="info-grid">
        <div class="info-item">
          <div class="info-label">Firmware Version</div>
          <div class="info-value" id="val-version">Loading...</div>
        </div>
        <div class="info-item">
          <div class="info-label">Running Partition</div>
          <div class="info-value" id="val-running">Loading...</div>
        </div>
        <div class="info-item">
          <div class="info-label">Target Partition</div>
          <div class="info-value" id="val-target">Loading...</div>
        </div>
        <div class="info-item">
          <div class="info-label">Free Memory</div>
          <div class="info-value" id="val-heap">Loading...</div>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="drop-zone" id="drop-zone">
        <svg viewBox="0 0 24 24">
          <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
          <polyline points="17 8 12 3 7 8"></polyline>
          <line x1="12" y1="3" x2="12" y2="15"></line>
        </svg>
        <div class="drop-zone-text">Click or drag firmware (.bin) file here</div>
        <div class="drop-zone-hint">Supports StopWatch-UserDemo.bin / firmware.bin (Max 5 MB)</div>
        <input type="file" id="file-input" class="file-input" accept=".bin">
      </div>

      <div class="file-details" id="file-details">
        <div class="file-name" id="file-name">firmware.bin</div>
        <div class="file-size" id="file-size">0 KB</div>
      </div>

      <div class="progress-section" id="progress-section">
        <div class="progress-bar-container">
          <div class="progress-bar" id="progress-bar"></div>
        </div>
        <div class="progress-meta">
          <span id="progress-status">Uploading & Flashing...</span>
          <span id="progress-percent">0%</span>
        </div>
      </div>

      <div class="alert alert-success" id="alert-success"></div>
      <div class="alert alert-danger" id="alert-danger"></div>

      <button class="btn" id="upload-btn" disabled>
        <span>Flash Firmware</span>
      </button>
    </div>

    <div class="footer">
      M5Stack StopWatch &bull; ESP32-S3 &bull; <a href="/badge/state" id="badge-link">Badge Config</a>
    </div>
  </div>

  <script>
    (function() {
      const dropZone = document.getElementById('drop-zone');
      const fileInput = document.getElementById('file-input');
      const fileDetails = document.getElementById('file-details');
      const fileNameEl = document.getElementById('file-name');
      const fileSizeEl = document.getElementById('file-size');
      const uploadBtn = document.getElementById('upload-btn');
      const progressSection = document.getElementById('progress-section');
      const progressBar = document.getElementById('progress-bar');
      const progressStatus = document.getElementById('progress-status');
      const progressPercent = document.getElementById('progress-percent');
      const alertSuccess = document.getElementById('alert-success');
      const alertDanger = document.getElementById('alert-danger');

      let selectedFile = null;

      function formatBytes(bytes) {
        if (bytes === 0) return '0 B';
        const k = 1024;
        const sizes = ['B', 'KB', 'MB', 'GB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return (bytes / Math.pow(k, i)).toFixed(2) + ' ' + sizes[i];
      }

      function loadInfo() {
        fetch('/ota/info')
          .then(r => r.json())
          .then(data => {
            document.getElementById('val-version').textContent = data.version || 'Unknown';
            document.getElementById('val-running').textContent = data.running_partition || 'ota_0';
            document.getElementById('val-target').textContent = data.target_partition || 'ota_1';
            document.getElementById('val-heap').textContent = formatBytes(data.free_heap || 0);
          })
          .catch(() => {
            document.getElementById('val-version').textContent = 'V0.5';
            document.getElementById('val-running').textContent = 'Active';
            document.getElementById('val-target').textContent = 'Next OTA';
            document.getElementById('val-heap').textContent = 'ESP32-S3';
          });
      }

      dropZone.addEventListener('click', () => fileInput.click());

      dropZone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropZone.classList.add('dragover');
      });

      dropZone.addEventListener('dragleave', () => {
        dropZone.classList.remove('dragover');
      });

      dropZone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropZone.classList.remove('dragover');
        if (e.dataTransfer.files.length > 0) {
          handleFile(e.dataTransfer.files[0]);
        }
      });

      fileInput.addEventListener('change', () => {
        if (fileInput.files.length > 0) {
          handleFile(fileInput.files[0]);
        }
      });

      function handleFile(file) {
        alertDanger.classList.remove('active');
        alertSuccess.classList.remove('active');

        if (!file.name.endsWith('.bin')) {
          alertDanger.textContent = 'Error: Only .bin firmware image files are supported.';
          alertDanger.classList.add('active');
          uploadBtn.disabled = true;
          return;
        }

        if (file.size > 5 * 1024 * 1024) {
          alertDanger.textContent = 'Warning: Selected file (' + formatBytes(file.size) + ') exceeds partition limit (5 MB).';
          alertDanger.classList.add('active');
        }

        selectedFile = file;
        fileNameEl.textContent = file.name;
        fileSizeEl.textContent = formatBytes(file.size);
        fileDetails.classList.add('active');
        uploadBtn.disabled = false;
      }

      uploadBtn.addEventListener('click', () => {
        if (!selectedFile) return;

        alertDanger.classList.remove('active');
        alertSuccess.classList.remove('active');
        uploadBtn.disabled = true;
        dropZone.style.pointerEvents = 'none';
        progressSection.classList.add('active');
        progressBar.style.width = '0%';
        progressPercent.textContent = '0%';
        progressStatus.textContent = 'Starting upload...';

        const startTime = Date.now();
        const xhr = new XMLHttpRequest();
        xhr.open('POST', '/ota/upload', true);
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');

        xhr.upload.onprogress = (e) => {
          if (e.lengthComputable) {
            const percent = Math.round((e.loaded / e.total) * 100);
            progressBar.style.width = percent + '%';
            progressPercent.textContent = percent + '%';
            
            const elapsed = (Date.now() - startTime) / 1000;
            const speed = elapsed > 0 ? (e.loaded / elapsed) : 0;
            progressStatus.textContent = 'Flashing: ' + formatBytes(e.loaded) + ' / ' + formatBytes(e.total) + ' (' + formatBytes(speed) + '/s)';
          }
        };

        xhr.onload = () => {
          if (xhr.status === 200) {
            let resp = {};
            try { resp = JSON.parse(xhr.responseText); } catch (e) {}
            progressBar.style.width = '100%';
            progressPercent.textContent = '100%';
            progressStatus.textContent = 'Done!';
            alertSuccess.textContent = (resp.message || 'Firmware update successful!') + ' Device is rebooting into new firmware. Page will reload in 6 seconds.';
            alertSuccess.classList.add('active');

            let countdown = 6;
            const timer = setInterval(() => {
              countdown--;
              if (countdown <= 0) {
                clearInterval(timer);
                window.location.reload();
              } else {
                alertSuccess.textContent = (resp.message || 'Firmware update successful!') + ' Rebooting... Reloading in ' + countdown + 's.';
              }
            }, 1000);
          } else {
            let errorMsg = 'Upload failed with HTTP ' + xhr.status;
            try {
              const resp = JSON.parse(xhr.responseText);
              if (resp.message) errorMsg = resp.message;
            } catch (e) {
              if (xhr.responseText) errorMsg = xhr.responseText;
            }
            alertDanger.textContent = 'Error: ' + errorMsg;
            alertDanger.classList.add('active');
            progressStatus.textContent = 'Failed';
            uploadBtn.disabled = false;
            dropZone.style.pointerEvents = 'auto';
          }
        };

        xhr.onerror = () => {
          alertDanger.textContent = 'Network error during firmware transfer. Please check connection and try again.';
          alertDanger.classList.add('active');
          progressStatus.textContent = 'Network Error';
          uploadBtn.disabled = false;
          dropZone.style.pointerEvents = 'auto';
        };

        xhr.send(selectedFile);
      });

      loadInfo();
    })();
  </script>
</body>
</html>
)rawliteral";

}  // namespace ota
