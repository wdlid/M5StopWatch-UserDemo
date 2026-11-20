# AGENTS.md — M5StopWatch-UserDemo

## 1. Environment & Toolchain Rules (CRITICAL)

To avoid slow filesystem scans, **DO NOT run `find /` or broad recursive searches across the root filesystem** to locate `pio`, `clang`, `idf.py`, or cross-compilers. Use the exact pre-configured paths below:

- **Primary Build System (PlatformIO / PIOArduino)**:
  - `pio` / `platformio`: `/usr/local/bin/pio` (also at `/root/.platformio/penv/bin/pio`, PlatformIO Core 6.2.0)
  - `esptool.py`: `/root/.platformio/penv/bin/esptool.py`
  - **ESP-IDF Framework**: `/root/.platformio/packages/framework-espidf` (ESP-IDF 5.5.x via PIOArduino `55.03.312`)
  - **Xtensa ESP32-S3 Cross-Compiler**: `/root/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++` (and `gcc`)
  - **CMake & Ninja**: `/root/.platformio/packages/tool-cmake/bin/cmake` and `/root/.platformio/packages/tool-ninja/ninja`
- **Host Compilers / Formatters**:
  - `clang` / `clang++`: `/usr/bin/clang`, `/usr/bin/clang++` (Ubuntu Clang 18)
  - `gcc` / `g++`: `/usr/bin/gcc`, `/usr/bin/g++`
- **Note on Standalone `idf.py`**:
  - Standalone ESP-IDF (`idf.py`) is **not** installed globally in `$PATH` on this host (used in Docker CI `espressif/idf:v5.5`). Locally, always build and verify using `pio run`.

---

## 2. Build, Flash & Verification Commands

### Build Firmware (Local Default)
```bash
# Build main watch firmware
pio run -e m5stack-stopwatch

# Build lightweight recovery firmware (1.5 MB ota_1 partition)
pio run -e m5stack-stopwatch-recovery
```
- `scripts/fetch_components.py` runs automatically as a PlatformIO `pre:` script to clone/checkout/patch external dependencies listed in `[m5_components]` in `platformio.ini`.
- Main build outputs are written to `.pio/build/m5stack-stopwatch/` (`firmware.bin`, `bootloader.bin`, `partitions.bin`, `ota_data_initial.bin`).
- Recovery build outputs are written to `.pio/build/m5stack-stopwatch-recovery/` (`firmware.bin`).

### Generate Merged Factory Binary (0x0 Offset)
```bash
python3 scripts/merge_factory_bin.py
```
Or manually via esptool:
```bash
/root/.platformio/penv/bin/python -m esptool --chip esp32s3 merge_bin \
  -o m5stopwatch-factory-0x0.bin \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 .pio/build/m5stack-stopwatch/bootloader.bin \
  0x8000 .pio/build/m5stack-stopwatch/partitions.bin \
  0xd000 .pio/build/m5stack-stopwatch/ota_data_initial.bin \
  0x20000 .pio/build/m5stack-stopwatch/firmware.bin \
  0x5A0000 .pio/build/m5stack-stopwatch-recovery/firmware.bin
```
> **Important Hardware Constraint**: ESP32-S3 with 16 MB Flash and 8 MB Octal (OPI) PSRAM **requires `dio` flash mode** (`--flash_mode dio`), never `qio`.

---

## 3. Hardware Overview (M5Stack StopWatch)

- **MCU**: ESP32-S3 (`esp32s3box` base board config), 16 MB Flash (DIO, 80 MHz), 8 MB OPI PSRAM.
- **Display**: 466×466 circular AMOLED display (`SH8601Z` / M5GFX + LVGL v9.5.0).
- **Peripherals**:
  - **PMIC**: `M5PM1` (battery management, power button, green power indicator LED).
  - **IO Expander**: `M5IOE1` (touch reset, speaker amplifier enable, vibration motor, power off).
  - **IMU**: `BMI270` + `BMM150` 6-axis/magnetometer sensor.
  - **Audio**: ES8388 / `esp_codec_dev` microphone & speaker I2S codec.
  - **RTC**: Hardware RTC + SNTP time sync.
- **Partition Layout (`partitions.csv`)**:
  - `nvs` (`0x9000`, 16 KB), `otadata` (`0xd000`, 8 KB), `phy_init` (`0xf000`, 4 KB)
  - `ota_0` (`0x20000`, 5.5 MB): Main stopwatch application
  - `ota_1` (`0x5A0000`, 1.5 MB): Dedicated Recovery firmware (installer + emergency SoftAP)
  - `storage` (`0x720000`, ~8.81 MB / 9024 KB): FAT filesystem (`hal_fs.cpp`, stores badge images, staged OTA, and user assets)
  - `coredump` (`0xFF0000`, 64 KB)

---

## 4. Architecture & Codebase Structure

```text
M5StopWatch-UserDemo/
├── main/
│   ├── main.cpp               # Entry point (app_main): initializes HAL, UI HAL, and installs Mooncake apps
│   ├── CMakeLists.txt         # Auto-globs apps/*, assets/*, hal/* (.c/.cc/.cpp)
│   ├── hal/                   # Hardware Abstraction Layer (GetHAL() singleton)
│   │   ├── hal.h / hal.cpp    # Core HAL interface & LvglLockGuard
│   │   ├── hal_*.cpp          # Subsystems: display, audio, imu, wifi/network, ota, badge, rtc, alarm, pmic, ioe, fs, eyecare
│   │   ├── drivers/           # Low-level hardware drivers
│   │   └── utils/             # Button handler, captive portal / AP web UI assets
│   ├── apps/                  # Mooncake AppAbility implementations
│   │   ├── apps.h             # Master header for all apps
│   │   ├── common/            # FirmwareVersion (common.h), status_bar, arc_top_clock, key_manager, audio, loading_page, toast
│   │   ├── app_launcher/      # Circular launcher & FluidBox physics background
│   │   ├── app_watch_face/    # Multiple watch faces (classic, simple, big_number, number_flow)
│   │   ├── app_stopwatch/     # Stopwatch timer app
│   │   ├── app_alarm_clock/   # Alarm management & trigger view
│   │   ├── app_gemini/        # Gemini AI voice client, ECH connector, WebSocket/HTTP config server
│   │   ├── app_badge/         # Electronic badge viewer & Wi-Fi AP image manager
│   │   ├── app_fft/           # Real-time microphone audio spectrum visualizer
│   │   ├── app_imu/           # IMU visualizer
│   │   ├── app_lucky_wheel/   # Interactive lucky wheel game
│   │   ├── app_setup/         # System settings (brightness, color temp, volume, Wi-Fi, OTA update, factory reset)
│   │   └── app_template/      # Reference template for creating new apps
│   └── assets/                # Fonts, images, icons, sound effects (boot_sfx.bin)
├── components/                # External git components (auto-fetched, gitignored)
├── managed_components/        # ESP-IDF component manager packages (espressif__esp_codec_dev)
├── patches/                   # Local patches applied to M5IOE1 and M5PM1
├── scripts/
│   ├── fetch_components.py    # PlatformIO pre-build dependency fetcher & patcher
│   └── bump_version.py        # Increments FirmwareVersion in main/apps/common/common.h
├── platformio.ini             # Authoritative PlatformIO config & [m5_components] manifest
└── repos.json                 # Mirror dependency manifest for standalone fetch_repos.py
```

---

## 5. Development Conventions & Guidelines

1. **LVGL Thread Safety**:
   - LVGL runs on its own background update task.
   - **Always** acquire the LVGL mutex via `LvglLockGuard lock;` (or `GetHAL().lvglLock()` / `GetHAL().lvglUnlock()`) before creating, modifying, or deleting any LVGL / `smooth_ui_toolkit::lvgl_cpp` objects inside `onOpen()`, `onRunning()`, `onClose()`, or async callbacks.
2. **Mooncake App Lifecycle**:
   - Apps inherit from `mooncake::AppAbility` (`onCreate`, `onOpen`, `onRunning`, `onClose`).
   - Allocate UI widgets and `input::KeyManager` in `onOpen()`, and release/`.reset()` them inside `onClose()` (under `LvglLockGuard`) so inactive apps do not consume RAM.
   - Register new apps in `main/apps/apps.h` and `main/main.cpp` (`GetMooncake().installApp(...)`). Note that `main/CMakeLists.txt` uses `GLOB_RECURSE`, so new `.cpp` files under `main/apps/` are compiled automatically.
3. **Dependencies (`components/`)**:
   - Do not commit changes directly inside `components/*` (they are external clones managed by `platformio.ini` and `repos.json`).
   - If an external component needs modification, create/update a patch in `patches/` and reference it in both `platformio.ini` (`[m5_components]`) and `repos.json`.
4. **Code Formatting & Style**:
   - Follows `.clang-format` (Google-based, 4-space indentation, 120-column limit, `AfterFunction: true` braces, pointer alignment left `Type* var`).
   - Logging uses `mooncake_log` (`mclog::tagInfo`, `mclog::tagWarn`, `mclog::tagError`).
5. **Firmware Version Management**:
   - `common::FirmwareVersion` is defined in `main/apps/common/common.h` and automatically bumped by CI via `scripts/bump_version.py`.
