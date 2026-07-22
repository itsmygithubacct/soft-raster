#!/usr/bin/env python3
"""Render a small composed scene with every major binding family."""

from __future__ import annotations

from pathlib import Path
import sys

import soft_raster as sr


def moth_sprite() -> sr.Canvas:
    sprite = sr.Canvas(24, 24)
    sprite.fill_triangle(11, 12, 2, 4, 4, 14, 0xD9A441)
    sprite.fill_triangle(13, 12, 22, 4, 20, 14, 0xD9A441)
    sprite.fill_circle(12, 12, 3.2, 0x8A5A2B)
    sprite.fill_circle(12, 7.5, 2.2, 0xB2793C)
    sprite.line(10.5, 6, 8.5, 2.5, 1, 0x8A5A2B)
    sprite.line(13.5, 6, 15.5, 2.5, 1, 0x8A5A2B)
    sprite.pixel(11, 7, 0x241B1A).pixel(13, 7, 0x241B1A)
    return sprite


def render() -> sr.Canvas:
    scene = sr.Canvas(640, 360)
    for y in range(scene.height):
        amount = y / scene.height
        band = sr.mix(0x132238, 0xB95F50, amount * amount)
        scene.fill_rect(0, y, scene.width, 1, band)

    scene.fill_circle(490, 220, 53, 0xFFD166)
    scene.ring(490, 220, 68, 4, 0xFFB347, 0.55)
    scene.ring(490, 220, 88, 3, 0xFF9A5C, 0.30)
    scene.fill_triangle(-40, 300, 140, 190, 320, 300, 0x2C2438)
    scene.fill_triangle(180, 300, 400, 165, 640, 300, 0x241D30)
    scene.fill_rect(0, 296, 640, 64, 0x191426)
    scene.line(0, 297, 640, 297, 2, 0x62527B, 0.85)
    scene.line(48, 116, 590, 68, 3, 0xF8FAFC, 0.65, 12, 10)

    sprite = moth_sprite()
    scene.blit_alpha(sprite, 80, 88)
    scene.blit_scaled(sprite, 260, 60, 72, 72, 0.9)
    scene.blit_transformed(
        sprite,
        420,
        118,
        sr.Transform.FLIP_HORIZONTAL,
        0.85,
        tint=0x120E1C,
    )

    scene.fill_rect(120, 304, 400, 43, 0x0C0A14, 0.85)
    scene.stroke_rect(120, 304, 400, 43, 2, 0xD97B4A, 0.9)
    scene.text_center(320, 22, "SOFT-RASTER-PY", 0xFFD166, scale=3)
    scene.text_center(320, 77, "native pixels, python control", 0xF8FAFC)
    scene.text_center(320, 316, "dusk over the ridge", 0xD9C8A0)
    return scene


def main() -> int:
    output = Path(sys.argv[1] if len(sys.argv) > 1 else "demo.ppm")
    canvas = render()
    canvas.write_ppm(output)
    print(f"wrote {output} ({canvas.width}x{canvas.height})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
