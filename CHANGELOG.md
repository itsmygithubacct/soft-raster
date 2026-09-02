# Changelog

## Unreleased

- Add `sr_polyline()`, which strokes a chain of segments as one shape. Stroking
  a chain with one `sr_line()` per segment blends each shared vertex twice —
  measured at alpha 0.5, a plain segment pixel lands on 127 and the joint on
  191 — and restarts the dash phase at every vertex, so a dashed poly-line
  breaks its pattern at every bend. `sr_polyline()` takes coverage as the
  maximum over the segments rather than a sequence of blends, so each pixel is
  blended exactly once and interior vertices become round joins, and it
  measures the dash phase along the whole path plus a new `dash_offset`. Two
  points with `SR_CAP_ROUND` and no dash reproduce `sr_line()` byte for byte.
- Add `SR_CAP_ROUND`, `SR_CAP_BUTT` and `SR_CAP_SQUARE` for the two free ends
  of a stroked poly-line. A butt cap is what an arrowhead needs; a round cap
  under one shows as a blob past the tip.
- Add `sr_fill_polygon_aa()`. `sr_fill_triangle()`, `sr_fill_convex()` and
  `sr_fill_polygon()` sample the pixel center, so a scanline across a filled
  triangle contains only the values 0 and 255; beside `sr_fill_circle()`, whose
  rim carries every value between, the difference reads as a defect rather than
  a style. The new fill samples four sub-scanlines per row with exact
  horizontal span overlap, at about four times the cost, and agrees with
  `sr_fill_polygon()` on the interior.
- Add `sr_fill_round_rect()` and `sr_stroke_round_rect()`. A rounded rectangle
  assembled from `sr_fill_rect()` and `sr_fill_circle()` seams where the bars
  meet the arcs; coverage here comes from the exact signed distance to the
  shape, so both edges are anti-aliased and the stroke is a band around the
  outline. The stroke is centered on the outline like `sr_ring()`, not inside
  it like `sr_stroke_rect()`.
- Add `sr_flatten_cubic()`, adaptive cubic Bézier subdivision into points for
  `sr_polyline()`, bounded by `SR_CUBIC_MAX_DEPTH`. It reports the point count
  a curve needs whether or not a buffer was supplied, so a caller can size one
  in a first pass.
- Bump `SR_VERSION_MINOR` to 5. Every addition is additive; no existing call
  changed behaviour, and both existing test suites pass unmodified.

- Add `sr_pack_rgb()` for direct tightly packed RGB output, avoiding the
  intermediate RGBA allocation and channel-dropping pass used by presenters.
- Add per-call selectable embedded fonts, including the compact 7x14 face,
  while retaining the fixed 8x16 behavior of existing text calls.
- Add `sr_fill_polygon()`, an even-odd scanline fill that handles concave
  outlines. `sr_fill_convex()` keeps only pixels on the same side of every
  edge, which renders a concave polygon *smaller* than requested — an L comes
  out as the block where its arms overlap — so shapes such as a zone routed
  around an obstacle had no correct primitive.
- Fills are winding-independent and use half-open spans, so polygons sharing
  an edge tile it exactly once with no doubled blend. `sr_fill_convex()` treats
  boundaries as closed, so the two can differ by a pixel where a pixel center
  falls exactly on an edge.
- Document that `sr_fill_triangle()`, `sr_fill_convex()` and
  `sr_fill_polygon()` sample pixel centers and therefore have hard edges,
  unlike the anti-aliased primitives the header previously described in one
  blanket sentence.
- Bound primitive work to the active clip rectangle. This substantially
  reduces the cost of drawing a large primitive through a small clip.
- Replace division in nearest-neighbor scaling loops with exact quotient and
  remainder stepping, preserving the existing source-pixel mapping.
- Process PPM pixels in chunks instead of one stdio call per pixel, report
  header, raster, and close failures, and provide stable `errno` distinctions
  for malformed data, unsupported dimensions, allocation, and filesystem I/O.
- Make canvas dimension checks consistent across allocated and wrapped
  storage, fully reset canvas structs on lifecycle failures, saturate text
  width arithmetic, and reject non-finite geometry before integer conversion.
- Support overlapping unscaled blits within one canvas and add fast paths for
  fully opaque compositing.
- Add permanent native and Python benchmark targets plus regression coverage
  for RGB packing, extreme inputs, lifecycle reset, selectable fonts, and
  overlapping copies.
- Extend the native benchmark with diagonal line, thin ring, ellipse,
  screenful-of-text, and opaque rectangle cases, covering the primitives
  with the worst scaling behavior.
- Make `sr_blit_scaled()` and `sr_blit_transformed()` no-ops on an aliased
  canvas, matching `sr_scale_canvas()`, instead of silently corrupting
  pixels on input the non-overlap contract already declares invalid.
- Narrow `sr_line()` to per-row spans instead of scanning the segment's
  whole bounding box, making a full-frame diagonal line roughly 50x
  cheaper with byte-identical output (pinned by the new reference tests).
- Give fills the opaque fast path blits already had: fully covered
  `sr_fill_rect()` interiors become plain stores at alpha 1 (a full-frame
  opaque fill drops from ~16 ms to ~0.3 ms) and hoist the coverage
  conversion otherwise, and the uniform-alpha loops of
  `sr_fill_polygon()`, `sr_fill_triangle()`, and `sr_fill_convex()` do the
  same.  Output is byte-identical: an alpha-1 blend lands exactly on the
  color, and interior coverage is exactly 1.0.
- Rasterize `sr_ring()` as two chord runs per row instead of scanning the
  whole bounding box including the hollow interior, making a thin
  radius-300 ring roughly 17x cheaper with byte-identical output.
- Clear only the letterbox bars in `sr_scale_canvas()` instead of the
  whole destination the scale loop is about to overwrite, removing a full
  destination write from every aspect-matching present.

## 0.3.0 - 2026-07-22

- Add allocation-free `sr_blit_transformed()` composition with Tiled-order
  horizontal, vertical, and diagonal transforms, uniform alpha, optional tint,
  full-canvas clipping, and overflow-safe destination coordinates.
- Preserve the existing premultiplied-source pixel math for normal and tinted
  composition.

## 0.2.0

- Add shared ellipse, convex-polygon, P6 PPM, and embedded-font helpers.
