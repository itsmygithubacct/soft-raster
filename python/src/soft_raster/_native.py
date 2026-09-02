"""Native library discovery and typed ``ctypes`` declarations."""

from __future__ import annotations

import ctypes
import ctypes.util
from functools import lru_cache
import os
from pathlib import Path
import sys
from typing import Iterable


class SoftRasterError(RuntimeError):
    """Base exception for binding and native-library failures."""


class LibraryNotFoundError(SoftRasterError):
    """Raised when ``libsoft-raster`` cannot be loaded."""


class IncompatibleLibraryError(SoftRasterError):
    """Raised when a loaded library does not provide the required ABI."""


class _Canvas(ctypes.Structure):
    _fields_ = (
        ("px", ctypes.POINTER(ctypes.c_uint32)),
        ("w", ctypes.c_int),
        ("h", ctypes.c_int),
        ("clip_x0", ctypes.c_int),
        ("clip_y0", ctypes.c_int),
        ("clip_x1", ctypes.c_int),
        ("clip_y1", ctypes.c_int),
        ("owns_px", ctypes.c_bool),
    )


def _library_names() -> tuple[str, ...]:
    if sys.platform == "win32":
        return ("soft-raster.dll", "libsoft-raster.dll")
    if sys.platform == "darwin":
        return ("libsoft-raster.dylib", "libsoft-raster.so")
    return ("libsoft-raster.so",)


def _deduplicate(values: Iterable[str]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for value in values:
        if not value or value in seen:
            continue
        seen.add(value)
        result.append(value)
    return result


def _auto_candidates() -> list[str]:
    package = Path(__file__).resolve().parent
    project = package.parents[1]
    names = _library_names()
    candidates: list[str] = []
    for name in names:
        candidates.append(str(package / "_libs" / name))
        candidates.append(str(project.parent / "build" / name))
        candidates.append(str(project.parent / "soft-raster/build" / name))
        candidates.append(str(project.parent / "kilix/third_party/soft-raster/build" / name))
    found = ctypes.util.find_library("soft-raster")
    if found:
        candidates.append(found)
    candidates.extend(names)
    return _deduplicate(candidates)


class SoftRasterLibrary:
    """A loaded soft-raster library with the required 0.3 API.

    ``path`` can be an absolute path or a dynamic-loader name. When omitted,
    the loader checks ``SOFT_RASTER_LIBRARY``, a bundled ``_libs`` directory,
    the conventional sibling source checkout, and finally the system loader.
    Additive 0.4 RGB-packing and selectable-font APIs and the additive 0.5
    graph primitives are feature-detected.
    """

    abi_version = (0, 3)

    def __init__(self, path: str | os.PathLike[str] | None = None) -> None:
        explicit = os.fspath(path) if path is not None else None
        if explicit is not None and not isinstance(explicit, str):
            raise TypeError("library path must resolve to str")
        environment = os.environ.get("SOFT_RASTER_LIBRARY")
        candidates = (
            [explicit]
            if explicit
            else [environment]
            if environment
            else _auto_candidates()
        )
        errors: list[str] = []
        library: ctypes.CDLL | None = None
        loaded_from = ""
        for candidate in _deduplicate(value for value in candidates if value):
            expanded = os.path.abspath(os.path.expanduser(candidate)) if os.path.sep in candidate else candidate
            try:
                library = ctypes.CDLL(expanded, use_errno=True)
            except OSError as error:
                errors.append(f"{expanded}: {error}")
                continue
            loaded_from = expanded
            break
        if library is None:
            detail = "; ".join(errors) if errors else "no candidates"
            hint = "Set SOFT_RASTER_LIBRARY to the built shared library."
            raise LibraryNotFoundError(
                f"could not load libsoft-raster ({detail}). {hint}"
            )
        self.path = loaded_from
        self.raw = library
        self._declare_functions()

    def _declare(self, name: str, arguments: list[object], result: object) -> None:
        try:
            function = getattr(self.raw, name)
        except AttributeError as error:
            raise IncompatibleLibraryError(
                f"{self.path} does not export required symbol {name}; "
                "soft-raster 0.3 or a compatible later API is required"
            ) from error
        function.argtypes = arguments
        function.restype = result

    def _declare_optional(
        self, name: str, arguments: list[object], result: object
    ) -> bool:
        try:
            function = getattr(self.raw, name)
        except AttributeError:
            return False
        function.argtypes = arguments
        function.restype = result
        return True

    def _declare_functions(self) -> None:
        canvas = ctypes.POINTER(_Canvas)
        pixels = ctypes.POINTER(ctypes.c_uint32)
        bytes_pointer = ctypes.POINTER(ctypes.c_uint8)
        floats = ctypes.POINTER(ctypes.c_float)
        integer = ctypes.c_int
        floating = ctypes.c_float
        color = ctypes.c_uint32

        self._declare("sr_canvas_init", [canvas, integer, integer], ctypes.c_bool)
        self._declare("sr_canvas_wrap", [canvas, pixels, integer, integer], None)
        self._declare("sr_canvas_free", [canvas], None)
        self._declare(
            "sr_canvas_set_clip", [canvas, integer, integer, integer, integer], None
        )
        self._declare("sr_canvas_reset_clip", [canvas], None)
        self._declare(
            "sr_pack_rgba", [canvas, bytes_pointer, ctypes.c_size_t], ctypes.c_bool
        )
        self.supports_rgb_pack = self._declare_optional(
            "sr_pack_rgb", [canvas, bytes_pointer, ctypes.c_size_t], ctypes.c_bool
        )

        self._declare(
            "sr_rgb",
            [ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8],
            ctypes.c_uint32,
        )
        self._declare("sr_mix", [color, color, floating], ctypes.c_uint32)
        self._declare("sr_scale_rgb", [color, floating], ctypes.c_uint32)

        self._declare("sr_clear", [canvas, color], None)
        self._declare("sr_px", [canvas, integer, integer, color], None)
        self._declare("sr_blend", [canvas, integer, integer, color, floating], None)
        self._declare(
            "sr_fill_rect",
            [canvas, floating, floating, floating, floating, color, floating],
            None,
        )
        self._declare(
            "sr_stroke_rect",
            [
                canvas,
                floating,
                floating,
                floating,
                floating,
                floating,
                color,
                floating,
            ],
            None,
        )
        self._declare(
            "sr_fill_circle",
            [canvas, floating, floating, floating, color, floating],
            None,
        )
        self._declare(
            "sr_fill_ellipse",
            [canvas, floating, floating, floating, floating, color, floating],
            None,
        )
        self._declare(
            "sr_ring",
            [canvas, floating, floating, floating, floating, color, floating],
            None,
        )
        self._declare(
            "sr_line",
            [
                canvas,
                floating,
                floating,
                floating,
                floating,
                floating,
                color,
                floating,
                integer,
                integer,
            ],
            None,
        )
        self._declare(
            "sr_fill_triangle",
            [
                canvas,
                floating,
                floating,
                floating,
                floating,
                floating,
                floating,
                color,
                floating,
            ],
            None,
        )
        self._declare(
            "sr_fill_convex",
            [canvas, floats, floats, ctypes.c_size_t, color, floating],
            None,
        )
        self._declare(
            "sr_fill_polygon",
            [canvas, floats, floats, ctypes.c_size_t, color, floating],
            None,
        )

        graph_symbols = (
            self._declare_optional(
                "sr_polyline",
                [
                    canvas,
                    floats,
                    floats,
                    ctypes.c_size_t,
                    floating,
                    color,
                    floating,
                    integer,
                    integer,
                    floating,
                    integer,
                ],
                None,
            ),
            self._declare_optional(
                "sr_fill_polygon_aa",
                [canvas, floats, floats, ctypes.c_size_t, color, floating],
                None,
            ),
            self._declare_optional(
                "sr_fill_round_rect",
                [canvas, floating, floating, floating, floating, floating,
                 color, floating],
                None,
            ),
            self._declare_optional(
                "sr_stroke_round_rect",
                [canvas, floating, floating, floating, floating, floating,
                 floating, color, floating],
                None,
            ),
            self._declare_optional(
                "sr_flatten_cubic",
                [floating] * 9 + [floats, floats, ctypes.c_size_t],
                ctypes.c_size_t,
            ),
        )
        self.supports_graph_primitives = all(graph_symbols)

        self._declare("sr_text_width", [ctypes.c_char_p, integer], integer)
        self._declare(
            "sr_font_glyph", [ctypes.c_ubyte], ctypes.POINTER(ctypes.c_uint8)
        )
        text_arguments = [
            canvas,
            floating,
            floating,
            ctypes.c_char_p,
            color,
            floating,
            integer,
        ]
        self._declare("sr_text", text_arguments, None)
        self._declare("sr_text_center", text_arguments, None)
        self._declare("sr_text_outlined", text_arguments, None)
        self._declare("sr_text_shadow", text_arguments, None)
        font = ctypes.c_int
        font_symbols = (
            self._declare_optional("sr_font_advance", [font], integer),
            self._declare_optional("sr_font_height", [font], integer),
            self._declare_optional(
                "sr_font_glyph_in",
                [font, ctypes.c_ubyte],
                ctypes.POINTER(ctypes.c_uint8),
            ),
            self._declare_optional(
                "sr_text_width_in", [font, ctypes.c_char_p, integer], integer
            ),
            self._declare_optional("sr_text_in", [font, *text_arguments], None),
            self._declare_optional(
                "sr_text_center_in", [font, *text_arguments], None
            ),
        )
        self.supports_selectable_fonts = all(font_symbols)

        self._declare("sr_blit", [canvas, canvas, integer, integer], None)
        self._declare(
            "sr_blit_alpha", [canvas, canvas, integer, integer, floating], None
        )
        self._declare(
            "sr_blit_tint",
            [canvas, canvas, integer, integer, color, floating],
            None,
        )
        self._declare(
            "sr_blit_scaled",
            [canvas, canvas, integer, integer, integer, integer, floating],
            None,
        )
        self._declare(
            "sr_blit_transformed",
            [
                canvas,
                canvas,
                integer,
                integer,
                ctypes.c_uint8,
                floating,
                ctypes.c_bool,
                color,
            ],
            None,
        )
        self._declare("sr_scale_canvas", [canvas, canvas], None)
        self._declare("sr_load_ppm", [canvas, ctypes.c_char_p], ctypes.c_bool)
        self._declare("sr_write_ppm", [canvas, ctypes.c_char_p], ctypes.c_bool)

    def __repr__(self) -> str:
        return f"SoftRasterLibrary(path={self.path!r}, abi={self.abi_version!r})"


@lru_cache(maxsize=1)
def default_library() -> SoftRasterLibrary:
    """Load and cache the process-wide default library."""

    return SoftRasterLibrary()
