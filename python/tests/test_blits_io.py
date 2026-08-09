from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

import soft_raster as sr


class BlitTests(unittest.TestCase):
    @staticmethod
    def sprite() -> sr.Canvas:
        sprite = sr.Canvas(2, 3)
        sprite.pixels[:] = (
            0xFFFF0000,
            0xFF00FF00,
            0xFF0000FF,
            0xFFFFFF00,
            0xFFFF00FF,
            0xFF00FFFF,
        )
        return sprite

    def test_verbatim_alpha_tint_and_scaled_blits(self) -> None:
        source = self.sprite()
        destination = sr.Canvas(8, 8).clear(0x101010)
        destination.blit(source, 1, 1)
        self.assertEqual(destination[1, 1], 0xFFFF0000)
        self.assertEqual(destination[2, 3], 0xFF00FFFF)

        mask = sr.Canvas(2, 2)
        mask.fill_circle(1, 1, 1, 0xFFFFFF)
        destination.blit_tint(mask, 4, 1, 0xFF8800)
        self.assertNotEqual(destination[4, 1], 0xFF101010)

        scaled = sr.Canvas(4, 6).clear(0)
        scaled.blit_scaled(source, 0, 0, 4, 6)
        self.assertEqual(scaled[0, 0], 0xFFFF0000)
        self.assertEqual(scaled[3, 5], 0xFF00FFFF)

        transparent = sr.Canvas(1, 1).blend(0, 0, 0xFF0000, 0.5)
        alpha_target = sr.Canvas(1, 1).clear(0)
        alpha_target.blit_alpha(transparent, 0, 0)
        self.assertEqual(alpha_target[0, 0], 0xFF7F0000)

    def test_all_transform_axes_and_optional_tint(self) -> None:
        source = self.sprite()
        horizontal = sr.Canvas(2, 3).blit_transformed(
            source, 0, 0, sr.Transform.FLIP_HORIZONTAL
        )
        self.assertEqual(horizontal[0, 0], source[1, 0])
        self.assertEqual(horizontal[1, 2], source[0, 2])

        diagonal = sr.Canvas(3, 2).blit_transformed(
            source, 0, 0, sr.Transform.FLIP_DIAGONAL
        )
        self.assertEqual(diagonal[0, 0], source[0, 0])
        self.assertEqual(diagonal[2, 1], source[1, 2])

        both = sr.Canvas(2, 3).blit_transformed(
            source,
            0,
            0,
            sr.Transform.FLIP_HORIZONTAL | sr.Transform.FLIP_VERTICAL,
            tint="#abcdef",
        )
        self.assertEqual(both[0, 0], 0xFFABCDEF)
        with self.assertRaises(ValueError):
            both.blit_transformed(source, 0, 0, 8)

    def test_letterbox_scaling(self) -> None:
        source = sr.Canvas(4, 2).clear(0x336699)
        output = sr.Canvas(8, 8).scale_canvas(source)
        self.assertEqual(output[0, 0], 0xFF000000)
        self.assertEqual(output[0, 2], 0xFF336699)
        self.assertEqual(output[7, 5], 0xFF336699)
        self.assertEqual(output[7, 7], 0xFF000000)


class PackingAndImageTests(unittest.TestCase):
    def test_rgba_rgb_and_ppm_memory_formats(self) -> None:
        canvas = sr.Canvas(2, 1)
        canvas.pixels[:] = (0xFF010203, 0x7F102030)
        self.assertEqual(canvas.rgba_bytes(), b"\x01\x02\x03\xff\x10\x20\x30\x7f")
        self.assertEqual(canvas.rgb_bytes(), b"\x01\x02\x03\x10\x20\x30")
        self.assertEqual(canvas.ppm_bytes(), b"P6\n2 1\n255\n\x01\x02\x03\x10\x20\x30")

        output = bytearray(8)
        self.assertEqual(canvas.pack_rgba_into(output), 8)
        self.assertEqual(bytes(output), canvas.rgba_bytes())
        with self.assertRaises(BufferError):
            canvas.pack_rgba_into(bytearray(7))

        rgb_output = bytearray(6)
        self.assertEqual(canvas.pack_rgb_into(rgb_output), 6)
        self.assertEqual(bytes(rgb_output), canvas.rgb_bytes())
        with self.assertRaises(BufferError):
            canvas.pack_rgb_into(bytearray(5))
        with self.assertRaisesRegex(BufferError, "writable"):
            canvas.pack_rgb_into(bytes(6))
        with self.assertRaisesRegex(BufferError, "C-contiguous"):
            canvas.pack_rgba_into(memoryview(bytearray(16))[::2])

    def test_native_ppm_roundtrip_and_errors(self) -> None:
        canvas = sr.Canvas(3, 2).clear(0x123456)
        canvas.pixel(2, 1, 0xABCDEF)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "image.ppm"
            canvas.write_ppm(path)
            loaded = sr.Canvas.from_ppm(path)
            self.assertEqual(loaded.size, canvas.size)
            self.assertEqual(list(loaded.pixels), list(canvas.pixels))

            invalid = root / "invalid.ppm"
            invalid.write_bytes(b"not a ppm")
            with self.assertRaises(sr.ImageFormatError):
                sr.Canvas.from_ppm(invalid)
            invalid.write_bytes(b"P6\n999999999999999999999 1\n255\n")
            with self.assertRaises(sr.ImageFormatError):
                sr.Canvas.from_ppm(invalid)
            with self.assertRaises(FileNotFoundError):
                sr.Canvas.from_ppm(root / "missing.ppm")
            with self.assertRaises(OSError):
                canvas.write_ppm(root / "missing" / "image.ppm")


if __name__ == "__main__":
    unittest.main()
