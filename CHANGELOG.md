# Changelog

## Unreleased

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

## 0.3.0 - 2026-07-22

- Add allocation-free `sr_blit_transformed()` composition with Tiled-order
  horizontal, vertical, and diagonal transforms, uniform alpha, optional tint,
  full-canvas clipping, and overflow-safe destination coordinates.
- Preserve the existing premultiplied-source pixel math for normal and tinted
  composition.

## 0.2.0

- Add shared ellipse, convex-polygon, P6 PPM, and embedded-font helpers.
