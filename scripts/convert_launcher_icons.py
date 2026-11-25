#!/usr/bin/env python3
"""Rasterize launcher SVGs in main/assets/images/src/ into LVGL RGB565A8 .c assets.

Requires: rsvg-convert (librsvg2-bin), Pillow.
Default output size: 72x72 RGB565A8 (~15 KB flash each), matching orbital launcher badges 1:1.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "main/assets/images/src"
OUT = ROOT / "main/assets/images"

ICONS = [
    "icon_clock",
    "icon_setup",
    "icon_imu",
    "icon_gemini",
    "icon_stopwatch",
    "icon_badge",
]


def rgb_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def write_c(name: str, w: int, h: int, rgba: bytes, out_dir: Path) -> int:
    color = bytearray()
    alpha = bytearray()
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i : i + 4]
        if a == 0:
            color += b"\x00\x00"
            alpha.append(0)
        else:
            v = rgb_to_rgb565(r, g, b)
            color.append(v & 0xFF)
            color.append((v >> 8) & 0xFF)
            alpha.append(a)
    data = bytes(color) + bytes(alpha)
    assert len(data) == w * h * 3

    lines = [
        "#ifdef __has_include",
        '#if __has_include("lvgl.h")',
        "#ifndef LV_LVGL_H_INCLUDE_SIMPLE",
        "#define LV_LVGL_H_INCLUDE_SIMPLE",
        "#endif",
        "#endif",
        "#endif",
        "",
        "#if defined(LV_LVGL_H_INCLUDE_SIMPLE)",
        '#include "lvgl.h"',
        "#else",
        '#include "lvgl/lvgl.h"',
        "#endif",
        "",
        "#ifndef LV_ATTRIBUTE_MEM_ALIGN",
        "#define LV_ATTRIBUTE_MEM_ALIGN",
        "#endif",
        "",
        f"#ifndef LV_ATTRIBUTE_IMAGE_{name.upper()}",
        f"#define LV_ATTRIBUTE_IMAGE_{name.upper()}",
        "#endif",
        "",
        f"const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMAGE_{name.upper()} uint8_t",
        f"    {name}_map[] = {{",
    ]
    row: list[str] = []
    for b in data:
        row.append(f"0x{b:02x}")
        if len(row) == 18:
            lines.append("        " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("        " + ", ".join(row) + ",")
    lines.extend(
        [
            "};",
            "",
            f"const lv_image_dsc_t {name} = {{",
            "    .header.cf    = LV_COLOR_FORMAT_RGB565A8,",
            "    .header.magic = LV_IMAGE_HEADER_MAGIC,",
            f"    .header.w     = {w},",
            f"    .header.h     = {h},",
            f"    .data_size    = {w * h} * 3,",
            f"    .data         = {name}_map,",
            "};",
            "",
        ]
    )
    (out_dir / f"{name}.c").write_text("\n".join(lines))
    return len(data)


def render_svg(svg_path: Path, size: int, pad_ratio: float) -> Image.Image:
    """4x supersample then LANCZOS downscale for sharp edges at 1:1 display size."""
    text = svg_path.read_text(encoding="utf-8")
    box = max(1, int(size * (1.0 - 2 * pad_ratio)))
    render_px = box * 4
    with tempfile.NamedTemporaryFile(suffix=".svg", delete=False, mode="w", encoding="utf-8") as tf:
        tf.write(text)
        tmp_svg = Path(tf.name)
    tmp_png = Path(str(tmp_svg) + ".png")
    subprocess.check_call(
        [
            "rsvg-convert",
            "-w",
            str(render_px),
            "-h",
            str(render_px),
            "--keep-aspect-ratio",
            "-o",
            str(tmp_png),
            str(tmp_svg),
        ]
    )
    glyph = Image.open(tmp_png).convert("RGBA")
    tmp_svg.unlink(missing_ok=True)
    tmp_png.unlink(missing_ok=True)

    glyph.thumbnail((box, box), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    x = (size - glyph.width) // 2
    y = (size - glyph.height) // 2
    canvas.paste(glyph, (x, y), glyph)
    return canvas


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--size", type=int, default=72)
    parser.add_argument("--pad", type=float, default=0.03)
    args = parser.parse_args()

    total = 0
    for name in ICONS:
        svg = SRC / f"{name}.svg"
        if not svg.exists():
            raise SystemExit(f"missing {svg}")
        img = render_svg(svg, args.size, args.pad)
        nbytes = write_c(name, args.size, args.size, img.tobytes(), OUT)
        total += nbytes
        print(f"{name}: {nbytes / 1024:.1f} KB flash")
    print(f"total: {total / 1024:.1f} KB")


if __name__ == "__main__":
    main()
