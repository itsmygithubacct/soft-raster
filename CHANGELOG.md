# Changelog

## Unreleased

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

## 0.3.0 - 2026-07-22

- Add allocation-free `sr_blit_transformed()` composition with Tiled-order
  horizontal, vertical, and diagonal transforms, uniform alpha, optional tint,
  full-canvas clipping, and overflow-safe destination coordinates.
- Preserve the existing premultiplied-source pixel math for normal and tinted
  composition.

## 0.2.0

- Add shared ellipse, convex-polygon, P6 PPM, and embedded-font helpers.
