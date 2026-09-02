"""Tests for the additive 0.5 graph primitives."""

from __future__ import annotations

import unittest

import soft_raster as sr


def blue(canvas: sr.Canvas, x: int, y: int) -> int:
    return canvas[x, y] & 0xFF


class GraphPrimitiveTests(unittest.TestCase):
    def setUp(self) -> None:
        if not sr.default_library().supports_graph_primitives:
            self.skipTest("library predates the 0.5 graph primitives")

    def test_two_point_polyline_matches_line(self) -> None:
        with sr.Canvas(80, 40) as a, sr.Canvas(80, 40) as b:
            a.clear(0x000000)
            b.clear(0x000000)
            a.line(7.3, 6.1, 70.5, 33.9, 4.0, 0xFFFFFF, 0.75)
            b.polyline([(7.3, 6.1), (70.5, 33.9)], 4.0, 0xFFFFFF, 0.75)
            self.assertEqual(bytes(a.buffer), bytes(b.buffer))

    def test_joint_is_blended_once(self) -> None:
        with sr.Canvas(120, 60) as c:
            c.clear(0x000000)
            c.polyline([(10, 30), (60, 30), (110, 30)], 6.0, 0xFFFFFF, 0.5)
            self.assertEqual(blue(c, 60, 30), blue(c, 30, 30))

    def test_dash_phase_runs_along_the_whole_path(self) -> None:
        with sr.Canvas(200, 20) as c:
            c.clear(0x000000)
            c.polyline(
                [(0, 10), (100, 10), (200, 10)],
                2.0,
                0xFFFFFF,
                dash_on=8,
                dash_off=8,
                cap=sr.Cap.BUTT,
            )
            worst = run = 0
            for x in range(200):
                run = run + 1 if blue(c, x, 10) > 64 else 0
                worst = max(worst, run)
            self.assertLessEqual(worst, 8)

    def test_caps(self) -> None:
        points = [(10, 10), (30, 10)]
        with sr.Canvas(40, 20) as r, sr.Canvas(40, 20) as b:
            r.clear(0x000000)
            b.clear(0x000000)
            r.polyline(points, 6.0, 0xFFFFFF, cap=sr.Cap.ROUND)
            b.polyline(points, 6.0, 0xFFFFFF, cap=sr.Cap.BUTT)
            self.assertGreater(blue(r, 32, 10), 128)
            self.assertEqual(blue(b, 32, 10), 0)
            self.assertGreater(blue(b, 29, 10), 128)

    def test_anti_aliased_polygon_has_fractional_coverage(self) -> None:
        outline = [(8, 8), (56, 30), (8, 52)]
        with sr.Canvas(64, 64) as hard, sr.Canvas(64, 64) as soft:
            hard.clear(0x000000)
            soft.clear(0x000000)
            hard.fill_polygon(outline, 0xFFFFFF)
            soft.fill_polygon_aa(outline, 0xFFFFFF)
            values = {blue(soft, x, 14) for x in range(64)}
            self.assertTrue(any(0 < v < 255 for v in values))
            self.assertEqual(blue(hard, 12, 30), 255)
            self.assertEqual(blue(soft, 12, 30), 255)

    def test_round_rect_fill_and_stroke(self) -> None:
        with sr.Canvas(64, 48) as c:
            c.clear(0x000000)
            c.fill_round_rect(8, 8, 48, 32, 10, 0xFFFFFF)
            self.assertEqual(blue(c, 32, 24), 255)
            self.assertEqual(blue(c, 9, 9), 0)
        with sr.Canvas(64, 48) as c:
            c.clear(0x000000)
            c.stroke_round_rect(8, 8, 48, 32, 8, 3, 0xFFFFFF)
            self.assertEqual(blue(c, 32, 24), 0)
            self.assertEqual(blue(c, 32, 8), 255)

    def test_flatten_cubic(self) -> None:
        straight = sr.flatten_cubic(0, 0, 10, 0, 20, 0, 30, 0)
        self.assertEqual(straight, [(0.0, 0.0), (30.0, 0.0)])

        curved = sr.flatten_cubic(0, 0, 0, 100, 100, 100, 100, 0)
        self.assertGreater(len(curved), 8)
        self.assertLessEqual(len(curved), 1025)
        self.assertEqual(curved[0], (0.0, 0.0))
        self.assertEqual(curved[-1], (100.0, 0.0))

        loose = sr.flatten_cubic(0, 0, 0, 100, 100, 100, 100, 0, 8.0)
        self.assertLess(len(loose), len(curved))

    def test_curve_strokes_through_polyline(self) -> None:
        with sr.Canvas(120, 80) as c:
            c.clear(0x000000)
            c.polyline(sr.flatten_cubic(10, 70, 10, 10, 110, 10, 110, 70),
                       3.0, 0xFFFFFF)
            # B(0.5) = (P0 + 3*P1 + 3*P2 + P3) / 8 = (60, 25) for this curve.
            self.assertGreater(blue(c, 60, 25), 128)
            self.assertEqual(blue(c, 60, 60), 0)

    def test_invalid_input_is_rejected(self) -> None:
        with sr.Canvas(16, 16) as c:
            with self.assertRaises(ValueError):
                c.polyline([(1, 1)], 2.0, 0xFFFFFF)
            with self.assertRaises(ValueError):
                c.fill_polygon_aa([(1, 1), (2, 2)], 0xFFFFFF)
            with self.assertRaises(ValueError):
                c.polyline([(1, 1), (2, 2)], 2.0, 0xFFFFFF, cap=9)


if __name__ == "__main__":
    unittest.main()
