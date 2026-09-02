"""Python bindings for the soft-raster C software-rendering library."""

from ._native import (
    IncompatibleLibraryError,
    LibraryNotFoundError,
    SoftRasterError,
    SoftRasterLibrary,
    default_library,
)
from .canvas import (
    FONT_HEIGHT,
    FONT_WIDTH,
    Canvas,
    CanvasClosedError,
    Cap,
    ColorLike,
    Font,
    ImageFormatError,
    Point,
    Transform,
    color,
    flatten_cubic,
    font_advance,
    font_glyph,
    font_height,
    mix,
    rgb,
    scale_rgb,
    text_width,
)

__version__ = "0.1.0"
SOFT_RASTER_ABI = (0, 3)

__all__ = [
    "FONT_HEIGHT",
    "FONT_WIDTH",
    "SOFT_RASTER_ABI",
    "Canvas",
    "CanvasClosedError",
    "Cap",
    "ColorLike",
    "Font",
    "ImageFormatError",
    "IncompatibleLibraryError",
    "LibraryNotFoundError",
    "Point",
    "SoftRasterError",
    "SoftRasterLibrary",
    "Transform",
    "__version__",
    "color",
    "default_library",
    "flatten_cubic",
    "font_advance",
    "font_glyph",
    "font_height",
    "mix",
    "rgb",
    "scale_rgb",
    "text_width",
]
