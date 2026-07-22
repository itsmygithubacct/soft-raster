# Changelog

## 0.3.0 - 2026-07-22

- Add allocation-free `sr_blit_transformed()` composition with Tiled-order
  horizontal, vertical, and diagonal transforms, uniform alpha, optional tint,
  full-canvas clipping, and overflow-safe destination coordinates.
- Preserve the existing premultiplied-source pixel math for normal and tinted
  composition.

## 0.2.0

- Add shared ellipse, convex-polygon, P6 PPM, and embedded-font helpers.
