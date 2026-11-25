#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${1:-8080}"

echo "[1/2] Building WebAssembly LVGL preview in Docker..."
docker run --rm \
    -v "${ROOT_DIR}:/src" \
    -v m5sim-build:/build \
    emscripten/emsdk:latest \
    bash -c 'emcmake cmake -S /src/sim -B /build -DCMAKE_BUILD_TYPE=Release && cmake --build /build -j"$(nproc)" && mkdir -p /build/dist && cp /build/m5stopwatch_sim.html /build/dist/index.html && cp /build/m5stopwatch_sim.js /build/dist/m5stopwatch_sim.js && cp /build/m5stopwatch_sim.wasm /build/dist/m5stopwatch_sim.wasm'

echo "[2/2] Serving preview on http://0.0.0.0:${PORT} (Ctrl+C to stop)..."
docker run --rm -p "0.0.0.0:${PORT}:8080" -v m5sim-build:/build emscripten/emsdk:latest \
    python3 -m http.server 8080 --bind 0.0.0.0 --directory /build/dist
