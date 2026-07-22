from __future__ import annotations

import struct
import unittest

import soft_raster as sr


class CanvasLifecycleTests(unittest.TestCase):
    def test_canvas_starts_transparent_and_draws_opaque_pixels(self) -> None:
        canvas = sr.Canvas(4, 3)
        self.assertEqual(canvas.size, (4, 3))
        self.assertIs(canvas.library, sr.default_library())
        self.assertEqual(canvas.stride, 16)
        self.assertEqual(canvas.pixel_count, 12)
        self.assertEqual(canvas.clip, (0, 0, 4, 3))
        self.assertTrue(all(pixel == 0 for pixel in canvas.pixels))

        canvas.clear(0x102030).pixel(2, 1, "#aabbcc")
        self.assertEqual(canvas[0, 0], 0xFF102030)
        self.assertEqual(canvas[2, 1], 0xFFAABBCC)
        canvas[3, 2] = (1, 2, 3)
        self.assertEqual(canvas.get_pixel(3, 2), 0xFF010203)

    def test_external_buffer_is_wrapped_without_copying(self) -> None:
        storage = bytearray(4 * 4 * 4)
        canvas = sr.Canvas.from_buffer(storage, 4, 4)
        canvas.pixel(1, 1, 0x123456)
        self.assertEqual(struct.unpack_from("=I", storage, (1 * 4 + 1) * 4)[0], 0xFF123456)
        struct.pack_into("=I", storage, 0, 0xFFABCDEF)
        self.assertEqual(canvas[0, 0], 0xFFABCDEF)

    def test_exported_views_remain_safe_after_close(self) -> None:
        canvas = sr.Canvas(2, 2).clear(0x112233)
        pixels = canvas.pixels
        byte_view = canvas.buffer
        canvas.close()

        self.assertTrue(canvas.closed)
        self.assertEqual(pixels[0], 0xFF112233)
        pixels[1] = 0xFFAABBCC
        self.assertEqual(struct.unpack_from("=I", byte_view, 4)[0], 0xFFAABBCC)
        with self.assertRaises(sr.CanvasClosedError):
            canvas.clear(0)
        canvas.close()  # idempotent

    def test_context_manager_and_repr(self) -> None:
        with sr.Canvas(3, 2) as canvas:
            self.assertIn("width=3", repr(canvas))
        self.assertEqual(repr(canvas), "Canvas(closed=True)")

    def test_clip_context_restores_previous_clip(self) -> None:
        canvas = sr.Canvas(5, 5).clear(0)
        canvas.set_clip(1, 1, 3, 3)
        with canvas.clipped(2, 2, 1, 1):
            self.assertEqual(canvas.clip, (2, 2, 1, 1))
            canvas.fill_rect(0, 0, 5, 5, 0xFFFFFF)
        self.assertEqual(canvas.clip, (1, 1, 3, 3))
        self.assertEqual(canvas[2, 2], 0xFFFFFFFF)
        self.assertEqual(canvas[1, 1], 0xFF000000)
        canvas.reset_clip().pixel(0, 0, 0x112233)
        self.assertEqual(canvas[0, 0], 0xFF112233)

    def test_copy_preserves_pixels_and_clip_without_aliasing(self) -> None:
        original = sr.Canvas(3, 3).clear(0x102030).set_clip(1, 1, 1, 1)
        clone = original.copy()
        self.assertEqual(clone.clip, original.clip)
        self.assertEqual(list(clone.pixels), list(original.pixels))
        clone.reset_clip().pixel(0, 0, 0xFFFFFF)
        self.assertNotEqual(clone[0, 0], original[0, 0])

    def test_invalid_dimensions_buffers_and_indices_are_rejected(self) -> None:
        for width, height in ((0, 1), (1, 0), (-1, 2), (2**31 - 1, 2)):
            with self.subTest(width=width, height=height):
                with self.assertRaises(ValueError):
                    sr.Canvas(width, height)
        with self.assertRaises(BufferError):
            sr.Canvas(4, 4, buffer=bytes(64))
        with self.assertRaises(BufferError):
            sr.Canvas(4, 4, buffer=bytearray(63))
        with self.assertRaises(BufferError):
            sr.Canvas(2, 2, buffer=memoryview(bytearray(32))[::2])
        with self.assertRaises(BufferError):
            sr.Canvas(2, 2, buffer=memoryview(bytearray(17))[1:])
        canvas = sr.Canvas(2, 2)
        with self.assertRaises(IndexError):
            _ = canvas[-1, 0]
        with self.assertRaises(IndexError):
            canvas[2, 0] = 0


class ColorTests(unittest.TestCase):
    def test_color_forms_and_native_helpers(self) -> None:
        self.assertEqual(sr.color(0xFF010203), 0x010203)
        self.assertEqual(sr.color("#a0B1c2"), 0xA0B1C2)
        self.assertEqual(sr.color((1, 2, 3)), 0x010203)
        self.assertEqual(sr.rgb(1, 2, 3), 0x010203)
        self.assertEqual(sr.mix(0, 0xFFFFFF, 0.5), 0x7F7F7F)
        self.assertEqual(sr.scale_rgb(0x808080, 2), 0xFFFFFF)

    def test_invalid_colors_and_nonfinite_numbers_are_rejected(self) -> None:
        for value in ("#fff", "nothex", (1, 2), (-1, 2, 3), 0x1FFFFFFFF):
            with self.subTest(value=value):
                with self.assertRaises((TypeError, ValueError)):
                    sr.color(value)
        canvas = sr.Canvas(4, 4)
        with self.assertRaises(ValueError):
            canvas.fill_circle(float("nan"), 2, 1, 0)
        with self.assertRaises(ValueError):
            canvas.fill_rect(1e20, 0, 1, 1, 0)


if __name__ == "__main__":
    unittest.main()
