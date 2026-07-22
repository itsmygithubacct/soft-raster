from __future__ import annotations

import os
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import soft_raster as sr


class NativeLibraryTests(unittest.TestCase):
    def test_default_library_exposes_expected_abi(self) -> None:
        library = sr.default_library()
        self.assertEqual(library.abi_version, (0, 3))
        self.assertEqual(sr.SOFT_RASTER_ABI, (0, 3))
        self.assertIn("soft-raster", library.path)
        self.assertIn("abi=(0, 3)", repr(library))
        self.assertTrue(callable(library.raw.sr_fill_rect))

    def test_explicit_missing_library_has_actionable_error(self) -> None:
        missing = "/definitely/not/a/library/libsoft-raster.so"
        with self.assertRaises(sr.LibraryNotFoundError) as caught:
            sr.SoftRasterLibrary(missing)
        self.assertIn(missing, str(caught.exception))
        self.assertIn("SOFT_RASTER_LIBRARY", str(caught.exception))

    def test_environment_override_is_authoritative(self) -> None:
        missing = "/missing/from/environment/libsoft-raster.so"
        with patch.dict(os.environ, {"SOFT_RASTER_LIBRARY": missing}):
            with self.assertRaises(sr.LibraryNotFoundError) as caught:
                sr.SoftRasterLibrary()
        self.assertIn(missing, str(caught.exception))

    def test_missing_abi_symbol_is_rejected_at_load_time(self) -> None:
        with patch("soft_raster._native.ctypes.CDLL", return_value=SimpleNamespace()):
            with self.assertRaises(sr.IncompatibleLibraryError) as caught:
                sr.SoftRasterLibrary("fake-library.so")
        self.assertIn("sr_canvas_init", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
