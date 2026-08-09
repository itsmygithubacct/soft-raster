from __future__ import annotations

import os
import unittest
from unittest.mock import patch

import soft_raster as sr


class NativeLibraryTests(unittest.TestCase):
    def test_default_library_exposes_expected_abi(self) -> None:
        library = sr.default_library()
        self.assertEqual(library.abi_version, (0, 3))
        self.assertEqual(sr.SOFT_RASTER_ABI, (0, 3))
        self.assertTrue(library.supports_rgb_pack)
        self.assertTrue(library.supports_selectable_fonts)
        self.assertIn("soft-raster", library.path)
        self.assertIn("abi=(0, 3)", repr(library))
        self.assertTrue(callable(library.raw.sr_fill_rect))

    def test_explicit_missing_library_has_actionable_error(self) -> None:
        missing = "/definitely/not/a/library/libsoft-raster.so"
        with self.assertRaises(sr.LibraryNotFoundError) as caught:
            sr.SoftRasterLibrary(missing)
        self.assertIn(missing, str(caught.exception))
        self.assertIn("SOFT_RASTER_LIBRARY", str(caught.exception))

        with self.assertRaisesRegex(TypeError, "must resolve to str"):
            sr.SoftRasterLibrary(b"libsoft-raster.so")  # type: ignore[arg-type]

    def test_environment_override_is_authoritative(self) -> None:
        missing = "/missing/from/environment/libsoft-raster.so"
        with patch.dict(os.environ, {"SOFT_RASTER_LIBRARY": missing}):
            with self.assertRaises(sr.LibraryNotFoundError) as caught:
                sr.SoftRasterLibrary()
        self.assertIn(missing, str(caught.exception))

    def test_missing_abi_symbol_is_rejected_at_load_time(self) -> None:
        with patch("soft_raster._native.ctypes.CDLL", return_value=object()):
            with self.assertRaises(sr.IncompatibleLibraryError) as caught:
                sr.SoftRasterLibrary("fake-library.so")
        self.assertIn("sr_canvas_init", str(caught.exception))

    def test_additive_04_symbols_are_optional_for_03_consumers(self) -> None:
        class WithoutAdditions:
            hidden = {
                "sr_pack_rgb",
                "sr_font_advance",
                "sr_font_height",
                "sr_font_glyph_in",
                "sr_text_width_in",
                "sr_text_in",
                "sr_text_center_in",
            }

            def __init__(self, raw: object) -> None:
                self.raw = raw

            def __getattr__(self, name: str) -> object:
                if name in self.hidden:
                    raise AttributeError(name)
                return getattr(self.raw, name)

        compatible = WithoutAdditions(sr.default_library().raw)
        with patch("soft_raster._native.ctypes.CDLL", return_value=compatible):
            library = sr.SoftRasterLibrary("compatible-0.3.so")
        self.assertFalse(library.supports_rgb_pack)
        self.assertFalse(library.supports_selectable_fonts)
        canvas = sr.Canvas(2, 1, library=library).clear(0x123456)
        self.assertEqual(canvas.rgb_bytes(), bytes.fromhex("123456123456"))
        canvas.text(0, 0, "A", 0xFFFFFF)
        self.assertEqual(sr.text_width("AB", library=library), 16)
        self.assertEqual(sr.font_height(library=library), 16)
        with self.assertRaises(sr.IncompatibleLibraryError):
            canvas.text(0, 0, "A", 0xFFFFFF, font=sr.Font.COMPACT_7X14)


if __name__ == "__main__":
    unittest.main()
