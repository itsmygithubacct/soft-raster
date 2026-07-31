from __future__ import annotations

import unittest

import soft_raster as sr


def red(pixel: int) -> int:
    return (pixel >> 16) & 0xFF


class PrimitiveTests(unittest.TestCase):
    def test_blend_matches_fixed_point_reference(self) -> None:
        canvas = sr.Canvas(3, 2).clear(0)
        canvas.blend(0, 0, 0xFFFFFF, 0.5)
        self.assertEqual(canvas[0, 0], 0xFF7F7F7F)
        sprite = sr.Canvas(2, 2)
        sprite.blend(0, 0, 0xFF0000, 0.5)
        self.assertEqual(sprite[0, 0], 0x7F7F0000)

    def test_fractional_rectangle_matches_c_reference(self) -> None:
        canvas = sr.Canvas(8, 8).clear(0)
        canvas.fill_rect(1.5, 1.5, 2, 2, 0xFFFFFF)
        self.assertEqual(red(canvas[1, 1]), 63)
        self.assertEqual(red(canvas[2, 1]), 127)
        self.assertEqual(red(canvas[2, 2]), 255)
        self.assertEqual(red(canvas[4, 4]), 0)

    def test_circle_ring_ellipse_and_stroke_rect(self) -> None:
        circle = sr.Canvas(16, 16).clear(0)
        circle.fill_circle(8, 8, 4, 0xFFFFFF)
        self.assertEqual(red(circle[8, 8]), 255)
        self.assertEqual(red(circle[11, 8]), 246)
        self.assertEqual(red(circle[12, 8]), 0)

        ring = sr.Canvas(16, 16).clear(0)
        ring.ring(8, 8, 5, 2, 0xFFFFFF)
        self.assertEqual(red(ring[12, 8]), 255)
        self.assertEqual(red(ring[8, 8]), 0)

        ellipse = sr.Canvas(20, 14).clear(0)
        ellipse.fill_ellipse(10, 7, 6, 3, 0x55AAFF)
        self.assertEqual(ellipse[10, 7], 0xFF55AAFF)
        self.assertEqual(ellipse[0, 0], 0xFF000000)

        outline = sr.Canvas(10, 10).clear(0)
        outline.stroke_rect(1, 1, 8, 8, 1, 0xFFFFFF)
        self.assertEqual(outline[1, 1], 0xFFFFFFFF)
        self.assertEqual(outline[5, 5], 0xFF000000)

    def test_lines_triangles_and_convex_polygons(self) -> None:
        canvas = sr.Canvas(24, 16).clear(0)
        canvas.line(1, 4.5, 20, 4.5, 2, 0xFFFFFF, dash_on=4, dash_off=3)
        self.assertEqual(red(canvas[2, 4]), 255)
        self.assertEqual(red(canvas[7, 4]), 0)

        canvas.fill_triangle(2, 14, 8, 7, 14, 14, 0xFF0000)
        self.assertEqual(canvas[8, 11], 0xFFFF0000)
        canvas.fill_convex(((15, 7), (22, 8), (21, 14), (16, 14)), 0x00FF00)
        self.assertEqual(canvas[18, 10], 0xFF00FF00)
        with self.assertRaises(ValueError):
            canvas.fill_convex(((0, 0), (1, 1)), 0)

    def test_text_helpers_and_rendering(self) -> None:
        self.assertEqual(sr.text_width("ABC", 2), 48)
        self.assertEqual(sr.text_width("ABC", 0), 24)
        self.assertEqual(sr.text_width("é", 1), 8)
        glyph = sr.font_glyph("A")
        self.assertEqual(len(glyph), sr.FONT_HEIGHT)
        self.assertTrue(any(glyph))
        self.assertEqual(sr.font_glyph("é"), sr.font_glyph("?"))

        canvas = sr.Canvas(160, 72).clear(0x101018)
        canvas.text(2, 2, "plain", 0xFFFFFF)
        canvas.text_center(80, 20, "center", 0x38BDF8)
        canvas.text_outlined(2, 38, "outline", 0xFFD166)
        canvas.text_shadow(78, 38, "shadow", 0xF8FAFC)
        self.assertGreater(sum(pixel != 0xFF101018 for pixel in canvas.pixels), 100)


if __name__ == "__main__":
    unittest.main()
