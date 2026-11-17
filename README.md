# M5StopWatch-UserDemo
M5Stack StopWatch user demo for hardware evaluation.

## Build

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

### Tool Chains

[ESP-IDF v5.5.4](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/index.html)

### Build

```bash
idf.py build
```

### Flash

```bash
idf.py flash
```

## FluidBox Integration
With genuine thanks to [omiya-bonsai](https://github.com/omiya-bonsai) for the open-source "FluidBox" fluid simulation demo for the M5Stack Stopwatch.
Incorporated into the *M5StopWatch-UserDemo* project by [Simon J. Stuart](https://github.com/LK-Simon)

## Power LED Off while Display On
Incorporated into the *M5StopWatch-UserDemo* project by [Simon J. Stuart](https://github.com/LK-Simon) the green power indicator LED will now switch off when the AMOLED display is on. This will save a small amount of power, but also eliminate the distracting green LED when it is not necessary.

## PlatformIO/PIOArduino build support
Incorporated into the *M5StopWatch-UserDemo* project by [Simon J. Stuart](https://github.com/LK-Simon) you can now load the repository folder in Visual Studio Code with either PlatformIO or PIOArduino installed, and build/upload the demo directly to your M5Stack StopWatch device.

## Over-The-Air (OTA) Firmware Updates
The firmware includes an integrated Web OTA system:
- **On-Device Trigger**: Navigate to **Settings &rarr; Firmware &rarr; OTA Update**.
- **Wi-Fi Station Mode**: If connected to your local network, the device screen displays your local IP (e.g. `http://192.168.1.x/ota`).
- **SoftAP Mode**: If Wi-Fi is not connected, the device starts an OTA access point (`M5StopWatch-OTA-XXXX`) accessible at `http://192.168.4.1/ota`.
- **Badge Mode Access**: The `/ota` web update endpoint is also accessible when entering Badge Manager mode.
- **Web UI Features**:
  - Drag-and-drop firmware `.bin` upload.
  - Active & target partition status reporting (`ota_0` / `ota_1`).
  - Binary verification (ESP32 image magic header 0xE9 validation).
  - Live progress bar with upload speed, byte counter, and automatic reboot into updated firmware.

## GitHub CI/CD Pipeline
An automated GitHub Actions workflow is provided in [`.github/workflows/build-firmware.yml`](.github/workflows/build-firmware.yml):
- **Triggers**: On push to `main`, `master`, `feature/**`, `fix/**`, pull requests, and manual `workflow_dispatch`.
- **Toolchains**: Builds with both PlatformIO Core and ESP-IDF v5.5.
- **Artifacts Generated**:
  - `firmware-ota.bin`: Application binary ready for Web OTA upgrade.
  - `m5stopwatch-factory-0x0.bin`: Complete factory image merged at offset 0x0 (ready for ESP Web Flasher / esptool / M5Burner).
  - `bootloader.bin` & `partitions.bin`: Component partition binaries.
  - `SHA256SUMS`: Verification hashes.
- **Automated Releases**: Pushing a tag (`v*.*.*`) automatically publishes a GitHub Release with attached binaries and auto-generated release notes.
