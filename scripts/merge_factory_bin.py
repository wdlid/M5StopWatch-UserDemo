#!/usr/bin/env python3
"""
Merge bootloader, partitions, otadata, main firmware (ota_0), and recovery firmware (ota_1)
into a single 16MB factory image ready to flash at 0x0.
"""

import os
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_MAIN = PROJECT_ROOT / ".pio" / "build" / "m5stack-stopwatch"
BUILD_RECOVERY = PROJECT_ROOT / ".pio" / "build" / "m5stack-stopwatch-recovery"
OUTPUT_BIN = PROJECT_ROOT / "m5stopwatch-factory-0x0.bin"

ESPTOOL_PY = Path("/root/.platformio/penv/bin/esptool.py")
PYTHON_BIN = Path("/root/.platformio/penv/bin/python")

def check_files():
    required = {
        "Bootloader": BUILD_MAIN / "bootloader.bin",
        "Partitions": BUILD_MAIN / "partitions.bin",
        "OTA Data Initial": BUILD_MAIN / "ota_data_initial.bin",
        "Main Firmware": BUILD_MAIN / "firmware.bin",
        "Recovery Firmware": BUILD_RECOVERY / "firmware.bin",
    }
    missing = []
    for name, path in required.items():
        if not path.is_file():
            missing.append(f"  - {name}: {path}")
    if missing:
        print("[ERROR] Missing required binaries:")
        print("\n".join(missing))
        print("\nPlease build both environments first:")
        print("  pio run -e m5stack-stopwatch")
        print("  pio run -e m5stack-stopwatch-recovery")
        sys.exit(1)

def merge():
    check_files()
    cmd = [
        str(PYTHON_BIN),
        "-m", "esptool",
        "--chip", "esp32s3",
        "merge_bin",
        "-o", str(OUTPUT_BIN),
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "16MB",
        "0x0", str(BUILD_MAIN / "bootloader.bin"),
        "0x8000", str(BUILD_MAIN / "partitions.bin"),
        "0xd000", str(BUILD_MAIN / "ota_data_initial.bin"),
        "0x20000", str(BUILD_MAIN / "firmware.bin"),
        "0x5A0000", str(BUILD_RECOVERY / "firmware.bin"),
    ]
    print("[INFO] Merging binaries into 0x0 factory image...")
    print(" ".join(cmd))
    res = subprocess.run(cmd)
    if res.returncode == 0:
        print(f"\n[SUCCESS] Factory binary generated: {OUTPUT_BIN}")
        print(f"File size: {OUTPUT_BIN.stat().st_size / (1024 * 1024):.2f} MB")
    else:
        print(f"[FAILED] esptool merge_bin exited with code {res.returncode}")
        sys.exit(res.returncode)

if __name__ == "__main__":
    merge()
