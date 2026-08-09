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
    ColorLike,
    Font,
    ImageFormatError,
    Point,
    Transform,
    color,
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
    "font_advance",
    "font_glyph",
    "font_height",
    "mix",
    "rgb",
    "scale_rgb",
    "text_width",
]
