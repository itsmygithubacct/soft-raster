# Changelog

## Unreleased

- Add `Canvas.polyline()`, `Canvas.fill_polygon_aa()`,
  `Canvas.fill_round_rect()`, `Canvas.stroke_round_rect()`, `flatten_cubic()`
  and the `Cap` enum, binding the additive native 0.5 graph primitives. They
  are feature-detected through `SoftRasterLibrary.supports_graph_primitives`
  and report an actionable compatibility error against an older library, so a
  0.3 or 0.4 consumer is unaffected.

- Add `Canvas.fill_polygon()`, binding `sr_fill_polygon()` for concave
  outlines.
- Feature-detect the additive native 0.4 API. Use direct `sr_pack_rgb()` for
  `rgb_bytes()`, `pack_rgb_into()`, and `ppm_bytes()` when available while
  retaining the compatible 0.3 RGBA conversion path.
- Add `Font`, selectable-font text drawing, font-aware string metrics, glyph
  access, and face dimensions for the fixed 8x16 and compact 7x14 fonts. The
  fixed face remains available with a 0.3 library; compact selection reports
  an actionable compatibility error when the additive symbols are absent.
- Consolidate packed-output buffer validation and reject non-text filesystem
  paths before native-library discovery.

## 0.1.0 - 2026-07-22

- Add a complete `ctypes` binding for the soft-raster 0.3 API.
- Add Python-owned and caller-owned zero-copy canvas buffers with safe exported
  view lifetimes.
- Add clipped drawing, anti-aliased primitives, embedded-font text, sprite
  blits, Tiled-order transforms, letterbox scaling, and P6 PPM I/O.
- Add packed RGBA and RGB frame output for image and terminal presentation
  libraries.
- Add typed public APIs, automatic native-library discovery, examples, and
  native-backed tests.
