"""Manual Python binding benchmarks; not a pass/fail CI test."""

from __future__ import annotations

import time

import soft_raster as sr


WIDTH = 1280
HEIGHT = 720
PIXELS = WIDTH * HEIGHT


def report(name: str, iterations: int, started: float, pixels: int) -> None:
    elapsed = time.perf_counter() - started
    print(
        f"{name} pixels={pixels} iterations={iterations} "
        f"megapixels_s={pixels * iterations / elapsed / 1_000_000:.2f} "
        f"mean_ms={elapsed * 1000 / iterations:.3f}"
    )


def main() -> None:
    frame = sr.Canvas(WIDTH, HEIGHT).clear(0x101018)
    sprite = sr.Canvas(320, 180).fill_circle(
        160, 90, 88, 0x38BDF8, alpha=0.8
    )

    iterations = 100
    started = time.perf_counter()
    for index in range(iterations):
        frame.clear(index * 0x010101)
    report("python_clear", iterations, started, PIXELS)

    iterations = 16
    started = time.perf_counter()
    for _ in range(iterations):
        frame.fill_rect(0, 0, WIDTH, HEIGHT, 0x38BDF8, alpha=0.25)
    report("python_fill_rect", iterations, started, PIXELS)

    iterations = 80
    frame.set_clip(576, 296, 128, 128)
    started = time.perf_counter()
    for _ in range(iterations):
        frame.fill_rect(0, 0, WIDTH, HEIGHT, 0xF59E0B, alpha=0.25)
    report("python_fill_rect_clipped", iterations, started, 128 * 128)
    frame.reset_clip()

    iterations = 40
    started = time.perf_counter()
    for _ in range(iterations):
        packed = frame.rgba_bytes()
    report("python_rgba_bytes", iterations, started, PIXELS)
    assert len(packed) == PIXELS * 4

    iterations = 8
    started = time.perf_counter()
    for _ in range(iterations):
        packed = frame.rgb_bytes()
    report("python_rgb_bytes", iterations, started, PIXELS)
    assert len(packed) == PIXELS * 3

    iterations = 60
    started = time.perf_counter()
    for _ in range(iterations):
        frame.blit_alpha(sprite, 320, 180, alpha=0.75)
    report("python_blit_alpha", iterations, started, sprite.pixel_count)

    iterations = 4
    started = time.perf_counter()
    for _ in range(iterations):
        document = frame.ppm_bytes()
    report("python_ppm_bytes", iterations, started, PIXELS)
    assert document.startswith(b"P6\n")

    sprite.close()
    frame.close()


if __name__ == "__main__":
    main()
