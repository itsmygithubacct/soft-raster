"""Pythonic, ownership-safe wrappers around the soft-raster C API."""

from __future__ import annotations

from contextlib import contextmanager
import ctypes
from enum import IntEnum, IntFlag
import errno
import math
import operator
import os
from pathlib import Path
from typing import Any, Iterable, Iterator, Sequence

from ._native import (
    IncompatibleLibraryError,
    SoftRasterError,
    SoftRasterLibrary,
    _Canvas,
    default_library,
)


INT_MIN = -(2**31)
INT_MAX = 2**31 - 1
MAX_SAFE_COORDINATE = 1_000_000_000.0
FONT_WIDTH = 8
FONT_HEIGHT = 16

ColorLike = int | str | Sequence[int]
Point = tuple[float, float]


class CanvasClosedError(SoftRasterError):
    """Raised when an operation targets a closed canvas."""


class ImageFormatError(SoftRasterError):
    """Raised when a PPM file cannot be decoded."""


class Transform(IntFlag):
    """Whole-canvas sprite transforms, in Tiled-compatible operation order."""

    NONE = 0
    FLIP_HORIZONTAL = 1 << 0
    FLIP_VERTICAL = 1 << 1
    FLIP_DIAGONAL = 1 << 2


class Font(IntEnum):
    """Embedded native font faces."""

    FIXED_8X16 = 0
    COMPACT_7X14 = 1


def _integer(value: Any, name: str, minimum: int = INT_MIN, maximum: int = INT_MAX) -> int:
    try:
        result = operator.index(value)
    except TypeError as error:
        raise TypeError(f"{name} must be an integer") from error
    if result < minimum or result > maximum:
        raise ValueError(f"{name} must be between {minimum} and {maximum}")
    return result


def _channel(value: Any, name: str) -> int:
    return _integer(value, name, 0, 255)


def _floating(value: Any, name: str, *, coordinate: bool = False) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError) as error:
        raise TypeError(f"{name} must be a real number") from error
    if not math.isfinite(result):
        raise ValueError(f"{name} must be finite")
    if coordinate and abs(result) > MAX_SAFE_COORDINATE:
        raise ValueError(
            f"{name} is outside the safe raster coordinate range "
            f"±{MAX_SAFE_COORDINATE:g}"
        )
    return result


def color(value: ColorLike) -> int:
    """Normalize ``0xRRGGBB``, ``#RRGGBB``, or a three-channel sequence."""

    if isinstance(value, str):
        text = value.strip()
        if text.startswith("#"):
            text = text[1:]
        if len(text) != 6:
            raise ValueError("color strings must use RRGGBB or #RRGGBB")
        try:
            return int(text, 16)
        except ValueError as error:
            raise ValueError("color strings must contain hexadecimal digits") from error
    if not isinstance(value, (bytes, bytearray)):
        try:
            if len(value) == 3:  # type: ignore[arg-type]
                red, green, blue = value  # type: ignore[misc]
                return (
                    _channel(red, "red") << 16
                    | _channel(green, "green") << 8
                    | _channel(blue, "blue")
                )
        except TypeError:
            pass
    packed = _integer(value, "color", 0, 0xFFFFFFFF)
    return packed & 0xFFFFFF


def _library(value: SoftRasterLibrary | None) -> SoftRasterLibrary:
    if value is None:
        return default_library()
    if not isinstance(value, SoftRasterLibrary):
        raise TypeError("library must be a SoftRasterLibrary")
    return value


def _font(value: Font | int) -> Font:
    try:
        return Font(value)
    except (TypeError, ValueError) as error:
        raise ValueError("font must be Font.FIXED_8X16 or Font.COMPACT_7X14") from error


class Cap(IntEnum):
    """End treatment for the free ends of a stroked polyline."""

    ROUND = 0
    BUTT = 1
    SQUARE = 2


def _cap(value: "Cap | int") -> "Cap":
    try:
        return Cap(value)
    except (TypeError, ValueError) as error:
        raise ValueError("cap must be Cap.ROUND, Cap.BUTT or Cap.SQUARE") from error


def _require_graph_primitives(native: SoftRasterLibrary) -> None:
    if not native.supports_graph_primitives:
        raise IncompatibleLibraryError(
            f"{native.path} does not export the graph-primitive API; "
            "soft-raster 0.5 or a compatible later API is required"
        )


def _require_selectable_fonts(native: SoftRasterLibrary) -> None:
    if not native.supports_selectable_fonts:
        raise IncompatibleLibraryError(
            f"{native.path} does not export the selectable-font API; "
            "soft-raster 0.4 or a compatible later API is required"
        )


def rgb(
    red: int,
    green: int,
    blue: int,
    *,
    library: SoftRasterLibrary | None = None,
) -> int:
    """Pack three byte channels using ``sr_rgb``."""

    native = _library(library)
    return int(
        native.raw.sr_rgb(
            _channel(red, "red"),
            _channel(green, "green"),
            _channel(blue, "blue"),
        )
    )


def mix(
    first: ColorLike,
    second: ColorLike,
    amount: float,
    *,
    library: SoftRasterLibrary | None = None,
) -> int:
    """Linearly mix two colors using ``sr_mix``; the amount is C-clamped."""

    native = _library(library)
    return int(
        native.raw.sr_mix(
            color(first), color(second), _floating(amount, "amount")
        )
    )


def scale_rgb(
    value: ColorLike,
    factor: float,
    *,
    library: SoftRasterLibrary | None = None,
) -> int:
    """Scale and saturate RGB channels using ``sr_scale_rgb``."""

    native = _library(library)
    return int(
        native.raw.sr_scale_rgb(
            color(value), _floating(factor, "factor")
        )
    )


def _text_bytes(value: str | bytes) -> bytes:
    if isinstance(value, bytes):
        if all(32 <= byte <= 126 for byte in value):
            return value
        return bytes(byte if 32 <= byte <= 126 else ord("?") for byte in value)
    if not isinstance(value, str):
        raise TypeError("text must be str or bytes")
    return "".join(
        character if 32 <= ord(character) <= 126 else "?" for character in value
    ).encode("ascii")


def flatten_cubic(
    x0: float,
    y0: float,
    x1: float,
    y1: float,
    x2: float,
    y2: float,
    x3: float,
    y3: float,
    tolerance: float = 0.25,
    *,
    library: SoftRasterLibrary | None = None,
) -> list[Point]:
    """Flatten a cubic Bezier into points for :meth:`Canvas.polyline`.

    Subdivision is adaptive: a nearly straight curve costs two points and a
    tight one costs as many as it needs, up to 1025.  ``tolerance`` is the
    permitted deviation in pixels and is clamped to at least 1/32.
    """

    native = _library(library)
    _require_graph_primitives(native)
    arguments = [
        _floating(value, name, coordinate=True)
        for value, name in (
            (x0, "x0"), (y0, "y0"), (x1, "x1"), (y1, "y1"),
            (x2, "x2"), (y2, "y2"), (x3, "x3"), (y3, "y3"),
        )
    ]
    arguments.append(_floating(tolerance, "tolerance", coordinate=True))
    needed = int(native.raw.sr_flatten_cubic(*arguments, None, None, 0))
    if needed == 0:
        return []
    array_type = ctypes.c_float * needed
    xs = array_type()
    ys = array_type()
    written = int(native.raw.sr_flatten_cubic(*arguments, xs, ys, needed))
    return [(float(xs[i]), float(ys[i])) for i in range(min(written, needed))]


def text_width(
    text: str | bytes,
    scale: int = 1,
    *,
    font: Font | int = Font.FIXED_8X16,
    library: SoftRasterLibrary | None = None,
) -> int:
    """Return the selected embedded font's advance width for a string."""

    native = _library(library)
    selected = _font(font)
    encoded = _text_bytes(text)
    checked_scale = _integer(scale, "scale")
    if selected is Font.FIXED_8X16:
        return int(native.raw.sr_text_width(encoded, checked_scale))
    _require_selectable_fonts(native)
    return int(
        native.raw.sr_text_width_in(
            int(selected), encoded, checked_scale
        )
    )


def font_advance(
    font: Font | int = Font.FIXED_8X16,
    *,
    library: SoftRasterLibrary | None = None,
) -> int:
    """Return the selected font's unscaled horizontal advance."""

    native = _library(library)
    selected = _font(font)
    if selected is Font.FIXED_8X16 and not native.supports_selectable_fonts:
        return FONT_WIDTH
    _require_selectable_fonts(native)
    return int(native.raw.sr_font_advance(int(selected)))


def font_height(
    font: Font | int = Font.FIXED_8X16,
    *,
    library: SoftRasterLibrary | None = None,
) -> int:
    """Return the selected font's unscaled cell height."""

    native = _library(library)
    selected = _font(font)
    if selected is Font.FIXED_8X16 and not native.supports_selectable_fonts:
        return FONT_HEIGHT
    _require_selectable_fonts(native)
    return int(native.raw.sr_font_height(int(selected)))


def font_glyph(
    character: str | bytes | int,
    *,
    font: Font | int = Font.FIXED_8X16,
    library: SoftRasterLibrary | None = None,
) -> bytes:
    """Return the selected embedded face's bitmap rows for one character."""

    if isinstance(character, str):
        if len(character) != 1:
            raise ValueError("character must contain exactly one character")
        code = ord(character)
    elif isinstance(character, bytes):
        if len(character) != 1:
            raise ValueError("character must contain exactly one byte")
        code = character[0]
    else:
        code = _integer(character, "character", 0, 255)
    if code > 255:
        code = ord("?")
    native = _library(library)
    selected = _font(font)
    if selected is Font.FIXED_8X16:
        pointer = native.raw.sr_font_glyph(code)
        height = FONT_HEIGHT
    else:
        _require_selectable_fonts(native)
        pointer = native.raw.sr_font_glyph_in(int(selected), code)
        height = font_height(selected, library=native)
    if not pointer:
        raise SoftRasterError("native font glyph lookup returned a null pointer")
    return ctypes.string_at(pointer, height)


def _path_bytes(path: str | bytes | os.PathLike[str] | os.PathLike[bytes]) -> bytes:
    value = os.fsencode(path)
    if b"\0" in value:
        raise ValueError("paths cannot contain NUL bytes")
    return value


class Canvas:
    """A drawable ``0xAARRGGBB`` canvas backed by Python-owned memory.

    The default constructor allocates a zeroed ``bytearray`` and wraps it with
    ``sr_canvas_wrap``. Supplying ``buffer=`` wraps any writable, C-contiguous
    buffer without copying. The C library therefore never frees Python memory,
    and exported pixel/buffer views remain valid even after :meth:`close`.
    """

    __slots__ = (
        "_library",
        "_canvas",
        "_storage",
        "_bytes",
        "_pixels",
        "_closed",
    )

    def __init__(
        self,
        width: int,
        height: int,
        *,
        buffer: Any | None = None,
        library: SoftRasterLibrary | None = None,
    ) -> None:
        width = _integer(width, "width", 1, INT_MAX)
        height = _integer(height, "height", 1, INT_MAX)
        pixels_count = width * height
        if pixels_count > INT_MAX:
            raise ValueError("width × height must not exceed INT_MAX pixels")
        byte_count = pixels_count * ctypes.sizeof(ctypes.c_uint32)
        native = _library(library)
        storage = bytearray(byte_count) if buffer is None else buffer
        try:
            view = memoryview(storage)
        except TypeError as error:
            raise TypeError("buffer must implement the writable buffer protocol") from error
        if view.readonly:
            raise BufferError("buffer must be writable")
        if not view.c_contiguous:
            raise BufferError("buffer must be C-contiguous")
        try:
            bytes_view = view.cast("B")
        except TypeError as error:
            raise BufferError("buffer cannot be viewed as contiguous bytes") from error
        if bytes_view.nbytes < byte_count:
            raise BufferError(
                f"buffer needs at least {byte_count} bytes, got {bytes_view.nbytes}"
            )
        bytes_view = bytes_view[:byte_count]
        address = ctypes.addressof(ctypes.c_ubyte.from_buffer(bytes_view))
        if address % ctypes.alignment(ctypes.c_uint32):
            raise BufferError("buffer address is not aligned for uint32 pixels")
        pixel_type = ctypes.c_uint32 * pixels_count
        pixel_array = pixel_type.from_buffer(bytes_view)
        canvas = _Canvas()
        native.raw.sr_canvas_wrap(
            ctypes.byref(canvas),
            ctypes.cast(pixel_array, ctypes.POINTER(ctypes.c_uint32)),
            width,
            height,
        )
        if not canvas.px or canvas.w != width or canvas.h != height or canvas.owns_px:
            raise SoftRasterError("sr_canvas_wrap rejected a valid Python buffer")
        self._library = native
        self._canvas = canvas
        self._storage = storage
        self._bytes = bytes_view
        self._pixels = pixel_array
        self._closed = False

    @classmethod
    def from_buffer(
        cls,
        buffer: Any,
        width: int,
        height: int,
        *,
        library: SoftRasterLibrary | None = None,
    ) -> "Canvas":
        """Wrap a writable C-contiguous buffer without copying it."""

        return cls(width, height, buffer=buffer, library=library)

    @classmethod
    def from_ppm(
        cls,
        path: str | bytes | os.PathLike[str] | os.PathLike[bytes],
        *,
        library: SoftRasterLibrary | None = None,
    ) -> "Canvas":
        """Load a P6 PPM and copy it into an ownership-safe Python canvas."""

        native = _library(library)
        encoded = _path_bytes(path)
        temporary = _Canvas()
        ctypes.set_errno(0)
        if not native.raw.sr_load_ppm(ctypes.byref(temporary), encoded):
            error_number = ctypes.get_errno()
            native.raw.sr_canvas_free(ctypes.byref(temporary))
            if error_number in (errno.EINVAL, errno.EOVERFLOW, errno.ERANGE):
                raise ImageFormatError(
                    f"could not decode P6 PPM: {os.fsdecode(encoded)}"
                )
            if error_number:
                raise OSError(error_number, os.strerror(error_number), os.fsdecode(encoded))
            if not Path(os.fsdecode(encoded)).exists():
                raise FileNotFoundError(errno.ENOENT, os.strerror(errno.ENOENT), os.fsdecode(encoded))
            raise ImageFormatError(f"could not decode P6 PPM: {os.fsdecode(encoded)}")
        try:
            result = cls(temporary.w, temporary.h, library=native)
            ctypes.memmove(
                result._canvas.px,
                temporary.px,
                result.pixel_count * ctypes.sizeof(ctypes.c_uint32),
            )
            return result
        finally:
            native.raw.sr_canvas_free(ctypes.byref(temporary))

    def _require_open(self) -> None:
        if self._closed:
            raise CanvasClosedError("canvas is closed")

    @property
    def closed(self) -> bool:
        return self._closed

    @property
    def library(self) -> SoftRasterLibrary:
        """Return the native library used for this canvas."""

        return self._library

    @property
    def width(self) -> int:
        self._require_open()
        return self._canvas.w

    @property
    def height(self) -> int:
        self._require_open()
        return self._canvas.h

    @property
    def size(self) -> tuple[int, int]:
        return self.width, self.height

    @property
    def stride(self) -> int:
        return self.width * ctypes.sizeof(ctypes.c_uint32)

    @property
    def pixel_count(self) -> int:
        return self.width * self.height

    @property
    def clip(self) -> tuple[int, int, int, int]:
        self._require_open()
        return (
            self._canvas.clip_x0,
            self._canvas.clip_y0,
            self._canvas.clip_x1 - self._canvas.clip_x0,
            self._canvas.clip_y1 - self._canvas.clip_y0,
        )

    @property
    def pixels(self) -> Any:
        """Return a mutable ctypes ``uint32[]`` view of native-order pixels."""

        self._require_open()
        return self._pixels

    @property
    def buffer(self) -> memoryview:
        """Return a zero-copy writable byte view of native ``0xAARRGGBB`` words."""

        self._require_open()
        return self._bytes

    def close(self) -> None:
        """Detach the C canvas; previously exported Python views remain valid."""

        if self._closed:
            return
        self._library.raw.sr_canvas_free(ctypes.byref(self._canvas))
        self._closed = True
        self._storage = None
        self._bytes = None
        self._pixels = None

    def __enter__(self) -> "Canvas":
        self._require_open()
        return self

    def __exit__(self, *exc_info: object) -> None:
        del exc_info
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

    def __repr__(self) -> str:
        if self._closed:
            return "Canvas(closed=True)"
        return f"Canvas(width={self._canvas.w}, height={self._canvas.h}, clip={self.clip!r})"

    def _point(self, x: Any, y: Any) -> tuple[int, int]:
        return _integer(x, "x"), _integer(y, "y")

    def get_pixel(self, x: int, y: int) -> int:
        """Read one native ``0xAARRGGBB`` pixel, bounds checked."""

        self._require_open()
        x, y = self._point(x, y)
        if x < 0 or x >= self._canvas.w or y < 0 or y >= self._canvas.h:
            raise IndexError("pixel coordinate is outside the canvas")
        return int(self._pixels[y * self._canvas.w + x])

    def __getitem__(self, coordinate: tuple[int, int]) -> int:
        try:
            x, y = coordinate
        except (TypeError, ValueError) as error:
            raise TypeError("canvas indices must be (x, y)") from error
        return self.get_pixel(x, y)

    def __setitem__(self, coordinate: tuple[int, int], value: ColorLike) -> None:
        try:
            x, y = coordinate
        except (TypeError, ValueError) as error:
            raise TypeError("canvas indices must be (x, y)") from error
        x, y = self._point(x, y)
        if x < 0 or x >= self.width or y < 0 or y >= self.height:
            raise IndexError("pixel coordinate is outside the canvas")
        self.pixel(x, y, value)

    def copy(self) -> "Canvas":
        self._require_open()
        result = type(self)(self.width, self.height, library=self._library)
        ctypes.memmove(result._canvas.px, self._canvas.px, self.pixel_count * 4)
        result.set_clip(*self.clip)
        return result

    def set_clip(self, x: int, y: int, width: int, height: int) -> "Canvas":
        self._require_open()
        self._library.raw.sr_canvas_set_clip(
            ctypes.byref(self._canvas),
            _integer(x, "x"),
            _integer(y, "y"),
            _integer(width, "width"),
            _integer(height, "height"),
        )
        return self

    def reset_clip(self) -> "Canvas":
        self._require_open()
        self._library.raw.sr_canvas_reset_clip(ctypes.byref(self._canvas))
        return self

    @contextmanager
    def clipped(self, x: int, y: int, width: int, height: int) -> Iterator["Canvas"]:
        """Temporarily replace the clip rectangle, restoring the prior clip."""

        previous = self.clip
        self.set_clip(x, y, width, height)
        try:
            yield self
        finally:
            if not self._closed:
                self.set_clip(*previous)

    def clear(self, value: ColorLike) -> "Canvas":
        self._require_open()
        self._library.raw.sr_clear(ctypes.byref(self._canvas), color(value))
        return self

    def pixel(self, x: int, y: int, value: ColorLike) -> "Canvas":
        self._require_open()
        x, y = self._point(x, y)
        self._library.raw.sr_px(ctypes.byref(self._canvas), x, y, color(value))
        return self

    def blend(
        self, x: int, y: int, value: ColorLike, alpha: float = 1.0
    ) -> "Canvas":
        self._require_open()
        x, y = self._point(x, y)
        self._library.raw.sr_blend(
            ctypes.byref(self._canvas), x, y, color(value), _floating(alpha, "alpha")
        )
        return self

    def fill_rect(
        self,
        x: float,
        y: float,
        width: float,
        height: float,
        value: ColorLike,
        alpha: float = 1.0,
    ) -> "Canvas":
        self._require_open()
        self._library.raw.sr_fill_rect(
            ctypes.byref(self._canvas),
            _floating(x, "x", coordinate=True),
            _floating(y, "y", coordinate=True),
            _floating(width, "width", coordinate=True),
            _floating(height, "height", coordinate=True),
            color(value),
            _floating(alpha, "alpha"),
        )
        return self

    def stroke_rect(
        self,
        x: float,
        y: float,
        width: float,
        height: float,
        line_width: float,
        value: ColorLike,
        alpha: float = 1.0,
    ) -> "Canvas":
        self._require_open()
        self._library.raw.sr_stroke_rect(
            ctypes.byref(self._canvas),
            _floating(x, "x", coordinate=True),
            _floating(y, "y", coordinate=True),
            _floating(width, "width", coordinate=True),
            _floating(height, "height", coordinate=True),
            _floating(line_width, "line_width", coordinate=True),
            color(value),
            _floating(alpha, "alpha"),
        )
        return self

    def fill_circle(
        self,
        center_x: float,
        center_y: float,
        radius: float,
        value: ColorLike,
        alpha: float = 1.0,
    ) -> "Canvas":
        self._require_open()
        self._library.raw.sr_fill_circle(
            ctypes.byref(self._canvas),
            _floating(center_x, "center_x", coordinate=True),
            _floating(center_y, "center_y", coordinate=True),
            _floating(radius, "radius", coordinate=True),
            color(value),
            _floating(alpha, "alpha"),
        )
        return self

    def fill_ellipse(
        self,
        center_x: float,
        center_y: float,
        radius_x: float,
        radius_y: float,
        value: ColorLike,
        alpha: float = 1.0,
    ) -> "Canvas":
        self._require_open()
        self._library.raw.sr_fill_ellipse(
            ctypes.byref(self._canvas),
            _floating(center_x, "center_x", coordinate=True),
            _floating(center_y, "center_y", coordinate=True),
            _floating(radius_x, "radius_x", coordinate=True),
            _floating(radius_y, "radius_y", coordinate=True),
            color(value),
            _floating(alpha, "alpha"),
        )
        return self

    def ring(
        self,
        center_x: float,
        center_y: float,
        radius: float,
        width: float,
        value: ColorLike,
        alpha: float = 1.0,
    ) -> "Canvas":
        self._require_open()
        self._library.raw.sr_ring(
            ctypes.byref(self._canvas),
            _floating(center_x, "center_x", coordinate=True),
            _floating(center_y, "center_y", coordinate=True),
            _floating(radius, "radius", coordinate=True),
            _floating(width, "width", coordinate=True),
            color(value),
            _floating(alpha, "alpha"),
        )
        return self

    def line(
        self,
        x0: float,
        y0: float,
        x1: float,
        y1: float,
        width: float,
        value: ColorLike,
        alpha: float = 1.0,
        dash_on: int = 0,
        dash_off: int = 0,
    ) -> "Canvas":
        self._require_open()
        self._library.raw.sr_line(
            ctypes.byref(self._canvas),
            _floating(x0, "x0", coordinate=True),
            _floating(y0, "y0", coordinate=True),
            _floating(x1, "x1", coordinate=True),
            _floating(y1, "y1", coordinate=True),
            _floating(width, "width", coordinate=True),
            color(value),
            _floating(alpha, "alpha"),
            _integer(dash_on, "dash_on", 0, INT_MAX),
            _integer(dash_off, "dash_off", 0, INT_MAX),
        )
        return self

    def fill_triangle(
        self,
        x0: float,
        y0: float,
        x1: float,
        y1: float,
        x2: float,
        y2: float,
        value: ColorLike,
        alpha: float = 1.0,
    ) -> "Canvas":
        self._require_open()
        self._library.raw.sr_fill_triangle(
            ctypes.byref(self._canvas),
            _floating(x0, "x0", coordinate=True),
            _floating(y0, "y0", coordinate=True),
            _floating(x1, "x1", coordinate=True),
            _floating(y1, "y1", coordinate=True),
            _floating(x2, "x2", coordinate=True),
            _floating(y2, "y2", coordinate=True),
            color(value),
            _floating(alpha, "alpha"),
        )
        return self

    def fill_convex(
        self,
        points: Iterable[Sequence[float]],
        value: ColorLike,
        alpha: float = 1.0,
    ) -> "Canvas":
        self._require_open()
        coordinates: list[Point] = []
        for index, point in enumerate(points):
            try:
                x, y = point
            except (TypeError, ValueError) as error:
                raise ValueError(f"point {index} must contain exactly x and y") from error
            coordinates.append(
                (
                    _floating(x, f"points[{index}].x", coordinate=True),
                    _floating(y, f"points[{index}].y", coordinate=True),
                )
            )
        if len(coordinates) < 3:
            raise ValueError("a convex polygon needs at least three points")
        array_type = ctypes.c_float * len(coordinates)
        xs = array_type(*(point[0] for point in coordinates))
        ys = array_type(*(point[1] for point in coordinates))
        self._library.raw.sr_fill_convex(
            ctypes.byref(self._canvas),
            xs,
            ys,
            len(coordinates),
            color(value),
            _floating(alpha, "alpha"),
        )
        return self

    def fill_polygon(
        self,
        points: Iterable[Sequence[float]],
        value: ColorLike,
        alpha: float = 1.0,
    ) -> "Canvas":
        """Fill an arbitrary polygon, concave permitted.

        ``fill_convex`` keeps only pixels on the same side of every edge, so a
        concave outline comes out smaller than asked for -- an L renders as the
        block where its arms overlap.  This walks scanlines instead and handles
        any simple polygon.

        Filling is even-odd, so winding direction does not matter and a
        self-intersecting outline leaves its doubly-enclosed region empty.
        Spans are half-open, so polygons sharing an edge tile it exactly once.
        """
        self._require_open()
        coordinates: list[Point] = []
        for index, point in enumerate(points):
            try:
                x, y = point
            except (TypeError, ValueError) as error:
                raise ValueError(f"point {index} must contain exactly x and y") from error
            coordinates.append(
                (
                    _floating(x, f"points[{index}].x", coordinate=True),
                    _floating(y, f"points[{index}].y", coordinate=True),
                )
            )
        if len(coordinates) < 3:
            raise ValueError("a polygon needs at least three points")
        array_type = ctypes.c_float * len(coordinates)
        xs = array_type(*(point[0] for point in coordinates))
        ys = array_type(*(point[1] for point in coordinates))
        self._library.raw.sr_fill_polygon(
            ctypes.byref(self._canvas),
            xs,
            ys,
            len(coordinates),
            color(value),
            _floating(alpha, "alpha"),
        )
        return self


    def polyline(
        self,
        points: Iterable[Sequence[float]],
        width: float,
        value: ColorLike,
        alpha: float = 1.0,
        dash_on: int = 0,
        dash_off: int = 0,
        dash_offset: float = 0.0,
        cap: "Cap | int" = Cap.ROUND,
    ) -> "Canvas":
        """Stroke a chain of segments as one shape.

        Calling :meth:`line` once per segment blends the shared vertex twice --
        at alpha 0.5 a plain pixel lands on 127 and the joint on 191 -- and
        restarts the dash phase at every vertex.  This blends each pixel once,
        gives interior vertices round joins, and measures the dash phase along
        the whole path plus ``dash_offset``.

        ``cap`` applies only to the two free ends.  Repeat the first point last
        to close the outline, which makes both ends interior.
        """
        self._require_open()
        _require_graph_primitives(self._library)
        xs, ys, count = self._point_arrays(points, minimum=2)
        self._library.raw.sr_polyline(
            ctypes.byref(self._canvas),
            xs,
            ys,
            count,
            _floating(width, "width", coordinate=True),
            color(value),
            _floating(alpha, "alpha"),
            _integer(dash_on, "dash_on", 0, INT_MAX),
            _integer(dash_off, "dash_off", 0, INT_MAX),
            _floating(dash_offset, "dash_offset", coordinate=True),
            int(_cap(cap)),
        )
        return self

    def fill_polygon_aa(
        self,
        points: Iterable[Sequence[float]],
        value: ColorLike,
        alpha: float = 1.0,
    ) -> "Canvas":
        """Fill a polygon with anti-aliased edges.

        :meth:`fill_polygon` and :meth:`fill_triangle` sample the pixel center,
        so their coverage is only ever 0 or 255.  Beside a :meth:`fill_circle`,
        whose rim carries the values in between, that reads as a defect -- which
        is exactly the pairing an arrowhead on an edge into a round node makes.
        Costs about four times :meth:`fill_polygon`.
        """
        self._require_open()
        _require_graph_primitives(self._library)
        xs, ys, count = self._point_arrays(points, minimum=3)
        self._library.raw.sr_fill_polygon_aa(
            ctypes.byref(self._canvas),
            xs,
            ys,
            count,
            color(value),
            _floating(alpha, "alpha"),
        )
        return self

    def fill_round_rect(
        self,
        x: float,
        y: float,
        w: float,
        h: float,
        radius: float,
        value: ColorLike,
        alpha: float = 1.0,
    ) -> "Canvas":
        """Fill a rounded rectangle, both edges anti-aliased.

        ``radius`` is clamped to half the smaller side; zero draws square
        corners.
        """
        self._require_open()
        _require_graph_primitives(self._library)
        self._library.raw.sr_fill_round_rect(
            ctypes.byref(self._canvas),
            _floating(x, "x", coordinate=True),
            _floating(y, "y", coordinate=True),
            _floating(w, "w", coordinate=True),
            _floating(h, "h", coordinate=True),
            _floating(radius, "radius", coordinate=True),
            color(value),
            _floating(alpha, "alpha"),
        )
        return self

    def stroke_round_rect(
        self,
        x: float,
        y: float,
        w: float,
        h: float,
        radius: float,
        line: float,
        value: ColorLike,
        alpha: float = 1.0,
    ) -> "Canvas":
        """Stroke a rounded rectangle, centered on the outline.

        Unlike :meth:`stroke_rect`, whose bars sit inside the rectangle, this
        straddles the outline the way :meth:`ring` does.
        """
        self._require_open()
        _require_graph_primitives(self._library)
        self._library.raw.sr_stroke_round_rect(
            ctypes.byref(self._canvas),
            _floating(x, "x", coordinate=True),
            _floating(y, "y", coordinate=True),
            _floating(w, "w", coordinate=True),
            _floating(h, "h", coordinate=True),
            _floating(radius, "radius", coordinate=True),
            _floating(line, "line", coordinate=True),
            color(value),
            _floating(alpha, "alpha"),
        )
        return self

    def _point_arrays(
        self, points: Iterable[Sequence[float]], *, minimum: int
    ) -> tuple[Any, Any, int]:
        coordinates: list[Point] = []
        for index, point in enumerate(points):
            try:
                x, y = point
            except (TypeError, ValueError) as error:
                raise ValueError(
                    f"point {index} must contain exactly x and y"
                ) from error
            coordinates.append(
                (
                    _floating(x, f"points[{index}].x", coordinate=True),
                    _floating(y, f"points[{index}].y", coordinate=True),
                )
            )
        if len(coordinates) < minimum:
            raise ValueError(f"at least {minimum} points are required")
        array_type = ctypes.c_float * len(coordinates)
        return (
            array_type(*(point[0] for point in coordinates)),
            array_type(*(point[1] for point in coordinates)),
            len(coordinates),
        )

    def _draw_text(
        self,
        function_name: str,
        x: float,
        y: float,
        text: str | bytes,
        value: ColorLike,
        alpha: float,
        scale: int,
        font: Font | int | None = None,
    ) -> "Canvas":
        self._require_open()
        function = getattr(self._library.raw, function_name)
        arguments = (
            ctypes.byref(self._canvas),
            _floating(x, "x", coordinate=True),
            _floating(y, "y", coordinate=True),
            _text_bytes(text),
            color(value),
            _floating(alpha, "alpha"),
            _integer(scale, "scale"),
        )
        if font is None:
            function(*arguments)
        else:
            function(int(_font(font)), *arguments)
        return self

    def text(
        self,
        x: float,
        y: float,
        text: str | bytes,
        value: ColorLike,
        alpha: float = 1.0,
        scale: int = 1,
        *,
        font: Font | int = Font.FIXED_8X16,
    ) -> "Canvas":
        selected = _font(font)
        if selected is Font.FIXED_8X16:
            return self._draw_text(
                "sr_text", x, y, text, value, alpha, scale
            )
        _require_selectable_fonts(self._library)
        return self._draw_text(
            "sr_text_in", x, y, text, value, alpha, scale, selected
        )

    def text_center(
        self,
        center_x: float,
        y: float,
        text: str | bytes,
        value: ColorLike,
        alpha: float = 1.0,
        scale: int = 1,
        *,
        font: Font | int = Font.FIXED_8X16,
    ) -> "Canvas":
        selected = _font(font)
        if selected is Font.FIXED_8X16:
            return self._draw_text(
                "sr_text_center", center_x, y, text, value, alpha, scale
            )
        _require_selectable_fonts(self._library)
        return self._draw_text(
            "sr_text_center_in",
            center_x,
            y,
            text,
            value,
            alpha,
            scale,
            selected,
        )

    def text_outlined(
        self,
        x: float,
        y: float,
        text: str | bytes,
        value: ColorLike,
        alpha: float = 1.0,
        scale: int = 1,
    ) -> "Canvas":
        return self._draw_text(
            "sr_text_outlined", x, y, text, value, alpha, scale
        )

    def text_shadow(
        self,
        x: float,
        y: float,
        text: str | bytes,
        value: ColorLike,
        alpha: float = 1.0,
        scale: int = 1,
    ) -> "Canvas":
        return self._draw_text("sr_text_shadow", x, y, text, value, alpha, scale)

    def _source(self, source: "Canvas") -> _Canvas:
        if not isinstance(source, Canvas):
            raise TypeError("source must be a Canvas")
        source._require_open()
        return source._canvas

    def blit(self, source: "Canvas", x: int, y: int) -> "Canvas":
        self._require_open()
        source_canvas = self._source(source)
        self._library.raw.sr_blit(
            ctypes.byref(self._canvas),
            ctypes.byref(source_canvas),
            _integer(x, "x"),
            _integer(y, "y"),
        )
        return self

    def blit_alpha(
        self, source: "Canvas", x: int, y: int, alpha: float = 1.0
    ) -> "Canvas":
        self._require_open()
        source_canvas = self._source(source)
        self._library.raw.sr_blit_alpha(
            ctypes.byref(self._canvas),
            ctypes.byref(source_canvas),
            _integer(x, "x"),
            _integer(y, "y"),
            _floating(alpha, "alpha"),
        )
        return self

    def blit_tint(
        self,
        source: "Canvas",
        x: int,
        y: int,
        value: ColorLike,
        alpha: float = 1.0,
    ) -> "Canvas":
        self._require_open()
        source_canvas = self._source(source)
        self._library.raw.sr_blit_tint(
            ctypes.byref(self._canvas),
            ctypes.byref(source_canvas),
            _integer(x, "x"),
            _integer(y, "y"),
            color(value),
            _floating(alpha, "alpha"),
        )
        return self

    def blit_scaled(
        self,
        source: "Canvas",
        x: int,
        y: int,
        width: int,
        height: int,
        alpha: float = 1.0,
    ) -> "Canvas":
        self._require_open()
        source_canvas = self._source(source)
        self._library.raw.sr_blit_scaled(
            ctypes.byref(self._canvas),
            ctypes.byref(source_canvas),
            _integer(x, "x"),
            _integer(y, "y"),
            _integer(width, "width"),
            _integer(height, "height"),
            _floating(alpha, "alpha"),
        )
        return self

    def blit_transformed(
        self,
        source: "Canvas",
        x: int,
        y: int,
        transform: Transform | int = Transform.NONE,
        alpha: float = 1.0,
        tint: ColorLike | None = None,
    ) -> "Canvas":
        self._require_open()
        source_canvas = self._source(source)
        try:
            flags = Transform(transform)
        except (TypeError, ValueError) as error:
            raise ValueError("transform contains unknown flags") from error
        if int(flags) & ~int(
            Transform.FLIP_HORIZONTAL | Transform.FLIP_VERTICAL | Transform.FLIP_DIAGONAL
        ):
            raise ValueError("transform contains unknown flags")
        self._library.raw.sr_blit_transformed(
            ctypes.byref(self._canvas),
            ctypes.byref(source_canvas),
            _integer(x, "x"),
            _integer(y, "y"),
            int(flags),
            _floating(alpha, "alpha"),
            tint is not None,
            0 if tint is None else color(tint),
        )
        return self

    def scale_canvas(self, source: "Canvas") -> "Canvas":
        """Letterbox-scale the entire source into this destination canvas."""

        self._require_open()
        source_canvas = self._source(source)
        self._library.raw.sr_scale_canvas(
            ctypes.byref(self._canvas), ctypes.byref(source_canvas)
        )
        return self

    @staticmethod
    def _writable_byte_view(buffer: Any, byte_count: int) -> memoryview:
        try:
            view = memoryview(buffer)
        except TypeError as error:
            raise TypeError("buffer must implement the writable buffer protocol") from error
        if view.readonly:
            raise BufferError("buffer must be writable")
        if not view.c_contiguous:
            raise BufferError("buffer must be C-contiguous")
        try:
            byte_view = view.cast("B")
        except TypeError as error:
            raise BufferError("buffer cannot be viewed as contiguous bytes") from error
        if byte_view.nbytes < byte_count:
            raise BufferError(
                f"buffer needs at least {byte_count} bytes, got {byte_view.nbytes}"
            )
        return byte_view

    def _pack_into(self, buffer: Any, channels: int, function_name: str) -> int:
        self._require_open()
        byte_count = self.pixel_count * channels
        byte_view = self._writable_byte_view(buffer, byte_count)
        output_type = ctypes.c_uint8 * byte_count
        output = output_type.from_buffer(byte_view)
        function = getattr(self._library.raw, function_name)
        if not function(
            ctypes.byref(self._canvas), output, byte_count
        ):
            raise SoftRasterError(
                f"{function_name} rejected a correctly sized buffer"
            )
        return byte_count

    def pack_rgba_into(self, buffer: Any) -> int:
        """Pack R,G,B,A bytes into a writable buffer and return bytes written."""

        return self._pack_into(buffer, 4, "sr_pack_rgba")

    def rgba_bytes(self) -> bytes:
        """Return the canvas packed in R,G,B,A byte order."""

        output = bytearray(self.pixel_count * 4)
        self.pack_rgba_into(output)
        return bytes(output)

    def pack_rgb_into(self, buffer: Any) -> int:
        """Pack R,G,B bytes into a writable buffer and return bytes written."""

        if self._library.supports_rgb_pack:
            return self._pack_into(buffer, 3, "sr_pack_rgb")
        self._require_open()
        byte_count = self.pixel_count * 3
        output = self._writable_byte_view(buffer, byte_count)
        rgba = self.rgba_bytes()
        output[0:byte_count:3] = rgba[0::4]
        output[1:byte_count:3] = rgba[1::4]
        output[2:byte_count:3] = rgba[2::4]
        return byte_count

    def rgb_bytes(self) -> bytes:
        """Return tightly packed RGB bytes, suitable for Kitty presentation."""

        output = bytearray(self.pixel_count * 3)
        self.pack_rgb_into(output)
        return bytes(output)

    def ppm_bytes(self) -> bytes:
        """Return a complete binary P6 PPM document without touching disk."""

        header = f"P6\n{self.width} {self.height}\n255\n".encode("ascii")
        output = bytearray(len(header) + self.pixel_count * 3)
        output[:len(header)] = header
        view = memoryview(output)
        try:
            self.pack_rgb_into(view[len(header):])
        finally:
            view.release()
        return bytes(output)

    def write_ppm(
        self, path: str | bytes | os.PathLike[str] | os.PathLike[bytes]
    ) -> None:
        """Write a P6 PPM through the native ``sr_write_ppm`` routine."""

        self._require_open()
        encoded = _path_bytes(path)
        ctypes.set_errno(0)
        if self._library.raw.sr_write_ppm(ctypes.byref(self._canvas), encoded):
            return
        error_number = ctypes.get_errno() or errno.EIO
        raise OSError(error_number, os.strerror(error_number), os.fsdecode(encoded))
