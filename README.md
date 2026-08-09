# soft-raster

`soft-raster` is a small C11 software rasterizer for 32-bit framebuffers:
anti-aliased primitives, two embedded bitmap fonts, sprite blits with
per-pixel alpha, P6 PPM loading, and an aspect-preserving letterbox scaler.

The code was extracted from the software renderer shared by the
terminal-lander family of terminal games (`terminal_lander`,
`kitty-brokeout`, `kilix-fishtank`, and friends), where the same block was
duplicated at the top of every game's renderer. The blending math is kept
identical, so frames drawn through this library match the original games
byte for byte on an opaque canvas.

The library is pure ISO C11 with no operating-system dependencies. Drawing is
fully clipped to the active canvas clip. Canvas creation allocates its pixel
buffer, and polygons above 64 vertices allocate a temporary crossing array;
ordinary drawing uses only canvas and stack memory.

## Build and test

```sh
make
make test
make sanitize
make benchmark
./build/demo
```

The final command renders a composed scene (gradient sky, anti-aliased
circles and rings, dashed lines, outlined text, sprite blits, letterbox
scaling) into `demo.ppm`. The test suite asserts exact pixel values for
clipped stores, blend fixed-point math, edge coverage of rectangles,
circles, rings, and dashed lines, glyph bits against the font table, blit
clipping at all four edges, scaler geometry, and the PPM writer.

The typed, pure-Python binding is maintained in [`python/`](python). It uses
the same shared library without bundling a second native implementation:

```sh
make python-check
make python-wheel
make python-benchmark
```

The wheel is written below `python/dist/`.

## Quick start

```c
#include "soft_raster.h"

sr_canvas frame;
if (!sr_canvas_init(&frame, 640, 360)) {
    /* allocation failed or dimensions were invalid */
}

sr_clear(&frame, 0x101018);
sr_fill_circle(&frame, 320.0f, 180.0f, 48.0f, 0xffd166, 1.0f);
sr_ring(&frame, 320.0f, 180.0f, 64.0f, 2.0f, 0x38bdf8, 0.6f);
sr_line(&frame, 20.0f, 20.0f, 620.0f, 340.0f, 2.0f, 0xf8fafc, 0.8f, 6, 4);
sr_text_center(&frame, 320.0f, 24.0f, "HELLO", 0xf8fafc, 1.0f, 2);

sr_write_ppm(&frame, "frame.ppm");
sr_canvas_free(&frame);
```

An application that already owns a pixel buffer can draw into it without
any allocation:

```c
sr_canvas view;
sr_canvas_wrap(&view, my_pixels, my_width, my_height);
/* draw... */
sr_canvas_free(&view);   /* never frees wrapped memory */
```

## Pixel format

A canvas is a row-major array of `uint32_t` pixels laid out as
`0xAARRGGBB`:

- Colors passed to drawing calls are `0x00RRGGBB`; the high byte of a color
  argument is ignored. This is the same convention the games use, where
  `0xRRGGBB` literals carry implicit opacity.
- `sr_clear()` and `sr_px()` store the color with alpha `0xFF`.
- `sr_blend()` and every primitive built on it quantize coverage to 1/256
  steps, move each RGB channel toward the color by that coverage, and
  saturate the alpha byte toward `0xFF` by the same coverage.
- On a canvas that starts fully transparent (`sr_canvas_init()` zeroes its
  pixels), drawing therefore builds a premultiplied-alpha sprite: RGB
  carries color scaled by coverage and the high byte carries coverage.
  `sr_blit_alpha()`, `sr_blit_tint()`, `sr_blit_scaled()`, and
  `sr_blit_transformed()` composite such sprites over another canvas using
  that per-pixel alpha, optionally multiplied by a uniform alpha.

## Canvas

`sr_canvas_init()` allocates and zeroes a `w*h` canvas, rejecting
non-positive dimensions and pixel counts that would overflow.
`sr_canvas_wrap()` points a canvas at caller-owned memory without copying.
`sr_canvas_free()` releases memory obtained from `sr_canvas_init()` and
never frees wrapped memory. `sr_canvas_set_clip()` limits subsequent pixel and
primitive drawing to a rectangle; `sr_canvas_reset_clip()` restores the full
canvas. Initialize or wrap only an empty canvas, and free it before reusing the
struct. `sr_pack_rgba()` and `sr_pack_rgb()` convert native pixels into
presenter-friendly channel order.

## Drawing

All primitive coordinates are floats measured in pixels; edges falling
between pixel centers receive fractional-coverage anti-aliasing.

- `sr_fill_rect` — axis-aligned rectangle with anti-aliased edges.
- `sr_stroke_rect` — rectangle outline built from four bars.
- `sr_fill_circle` — filled disc with an anti-aliased rim.
- `sr_fill_ellipse` — filled ellipse with an anti-aliased rim.
- `sr_ring` — circle outline of a given stroke width.
- `sr_line` — stroked segment with round caps and coverage from the
  distance to the segment; `dash_on`/`dash_off` give a dash pattern in
  pixels (`0, 0` for solid).
- `sr_fill_triangle` — filled triangle via edge functions, either winding.
- `sr_fill_convex` — filled convex polygon from parallel coordinate arrays.
- `sr_fill_polygon` — filled polygon, concave permitted, via even-odd
  scanlines. Winding-independent, half-open spans so adjacent polygons tile
  without a seam, and cheaper than `sr_fill_convex` on any polygon of size.

## Text

`sr_text`, `sr_text_center`, `sr_text_outlined`, and `sr_text_shadow` retain
the original fixed 8x16 console font at an integer scale factor (clamped to at
least 1). The `sr_*_in()` variants select either that face or the compact 7x14
face per call. Characters outside ASCII 32..126 render as `?`. Font metrics,
string widths, and bitmap rows are available through `sr_font_advance()`,
`sr_font_height()`, `sr_text_width_in()`, and `sr_font_glyph_in()`.

## Blits and scaling

- `sr_blit` — verbatim clipped copy, alpha byte included.
- `sr_blit_alpha` — composites the source over the destination using each
  pixel's alpha byte times a uniform alpha.
- `sr_blit_tint` — like `sr_blit_alpha` but replaces the source color,
  using source alpha purely as a mask.
- `sr_blit_scaled` — nearest-neighbor resample into a destination
  rectangle, composited like `sr_blit_alpha`.
- `sr_blit_transformed` — whole-source alpha composite with any combination
  of horizontal, vertical, and diagonal-exchange flags. Diagonal exchange
  runs first and swaps the output dimensions; optional tint uses source alpha
  as a mask. The flag values match Tiled's compact transform order.
- `sr_scale_canvas` — scales the whole source onto the destination with
  nearest-neighbor sampling, preserving aspect ratio, centered, with
  opaque black letterbox bars.

The unscaled `sr_blit`, `sr_blit_alpha`, and `sr_blit_tint` calls support
overlap within the same canvas. Scaled, transformed, and letterbox operations
require non-overlapping source and destination storage.

## PPM images

`sr_load_ppm()` reads a binary P6 PPM into an owned canvas, including files
whose headers contain comments. `sr_write_ppm()` writes the canvas with the
alpha byte dropped. These are handy for small sprite assets, debugging, and
golden-image tests; they are the only routines that touch the file system.

## Font

`src/font8x16.h` holds 8x16 bitmaps for ASCII 32..126, extracted from
`Lat15-Fixed16` as shipped in Debian's console-setup package, whose
copyright file states that all console fonts are public domain by nature.
The Fixed typeface descends from the public-domain X11 misc-fixed fonts;
out of caution, the permissive notice from that lineage is preserved in the
header.

## Install

```sh
make install PREFIX=/usr/local
```

The build produces static and shared libraries. Applications may instead
compile `src/soft_raster.c` directly. The library depends only on the C
standard library and `libm`; nothing in it requires POSIX.

The API is pre-1.0 and may change between minor releases. Version 0.4.0 adds
direct RGB packing and selectable font APIs without changing existing calls.

## License

MIT. See [LICENSE](LICENSE) for the complete notices, including the font
provenance note.
