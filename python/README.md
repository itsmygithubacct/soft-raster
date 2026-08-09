# soft-raster Python bindings

`soft-raster-py` is an ownership-safe Python binding for
[`soft-raster`](..), the small C11
software rasterizer shared by the terminal-lander family of games. It exposes
the complete soft-raster 0.3 API and feature-detects additive 0.4 APIs without
a Python extension module or runtime Python dependencies.

Python owns each canvas buffer and gives its address to C with
`sr_canvas_wrap()`. Drawing remains native, while Python
gets direct mutable pixel and byte views. Those exported views remain valid
even if their `Canvas` is explicitly closed.

## Requirements

- Python 3.10 or newer
- A compatible `libsoft-raster` 0.3 or later built as a shared library

From a source checkout:

```bash
git clone https://github.com/itsmygithubacct/soft-raster.git
cd soft-raster/python
make check
```

The loader finds `../build/libsoft-raster.so` automatically. An
installed or unusual build can be selected explicitly:

```bash
export SOFT_RASTER_LIBRARY=/path/to/libsoft-raster.so
python3 your_program.py
```

Discovery checks, in order: `SOFT_RASTER_LIBRARY`, a package-local `_libs`
directory, this repository's native build, conventional sibling checkouts, the
platform library registry, and the dynamic loader's default paths. A configured
environment override is authoritative, so a typo fails clearly rather than
loading a surprising ABI.

Install the pure Python package with:

```bash
python3 -m pip install .
```

The wheel intentionally does not duplicate the native library. `soft-raster`
remains the source of truth and can be upgraded independently within the
supported API line. A 0.4 library enables direct RGB packing and selectable
fonts; the binding retains its fixed-font and RGBA conversion paths for 0.3.

## Quick start

```python
import soft_raster as sr

with sr.Canvas(640, 360) as frame:
    frame.clear("#101018")
    frame.fill_circle(320, 180, 48, 0xFFD166)
    frame.ring(320, 180, 64, 2, 0x38BDF8, alpha=0.6)
    frame.line(20, 20, 620, 340, 2, 0xF8FAFC, 0.8, 6, 4)
    frame.text_center(320, 24, "HELLO", 0xF8FAFC, scale=2)
    frame.write_ppm("frame.ppm")
```

Methods return the canvas, so rendering can also be chained. Colors accept an
integer, `#RRGGBB`, or an `(r, g, b)` sequence.

## Canvas memory

New canvases are transparent and backed by a zeroed Python `bytearray`:

```python
frame = sr.Canvas(320, 180)
pixels = frame.pixels   # mutable ctypes uint32[57600]
raw = frame.buffer      # writable zero-copy byte memoryview

pixels[0] = 0xFF112233
assert frame[0, 0] == 0xFF112233
```

Pixels use native `uint32_t` words whose values are `0xAARRGGBB`. Use
`rgba_bytes()` or `rgb_bytes()` when a consumer needs portable channel order.
Both formats can also be written into caller-owned buffers with
`pack_rgba_into()` and `pack_rgb_into()`. RGB conversion goes directly through
the native `sr_pack_rgb()` path and is suitable for `kitty-frame-presenter`:

```python
presenter.present(
    frame.rgb_bytes(), frame.width, frame.height,
    terminal_columns, terminal_rows,
)
```

Existing storage can be wrapped without a copy:

```python
storage = bytearray(width * height * 4)
frame = sr.Canvas.from_buffer(storage, width, height)
```

The buffer must be writable, C-contiguous, sufficiently large, and aligned for
32-bit pixels. NumPy is optional, but native pixels can be viewed when it is
already in an application:

```python
pixels = numpy.frombuffer(frame.buffer, dtype=numpy.uint32)
pixels = pixels.reshape(frame.height, frame.width)
```

Do not mutate the same canvas concurrently. Native calls use `ctypes.CDLL` and
may run without Python's GIL, but soft-raster canvases themselves are not
synchronized.

## Drawing API

`Canvas` covers every native drawing family:

- Pixels: `clear`, `pixel`, `blend`, tuple indexing.
- Primitives: `fill_rect`, `stroke_rect`, `fill_circle`, `fill_ellipse`,
  `ring`, `line`, `fill_triangle`, `fill_convex`, and `fill_polygon`.
- Embedded text: `text` and `text_center` accept `Font.FIXED_8X16` or
  `Font.COMPACT_7X14`; module-level `text_width`, `font_advance`,
  `font_height`, and `font_glyph` use the same selection. `text_outlined` and
  `text_shadow` retain the fixed face.
- Sprites: `blit`, `blit_alpha`, `blit_tint`, `blit_scaled`, and
  `blit_transformed` with `Transform` flags.
- Whole-frame letterboxing: `destination.scale_canvas(source)`.
- Images: `Canvas.from_ppm`, `write_ppm`, and in-memory `ppm_bytes`.

All primitive coordinates are checked for finite values before crossing the C
boundary. Text outside printable ASCII is rendered as `?`, matching the native
font contract.

Clipping can be persistent or scoped:

```python
frame.set_clip(10, 10, 100, 80)
with frame.clipped(20, 20, 40, 30):
    frame.fill_rect(0, 0, 200, 200, "#38bdf8")
frame.reset_clip()
```

## Demo and packaging

```bash
make demo              # writes demo.ppm
make wheel             # writes a pure-Python wheel to dist/
make -C .. python-benchmark
```

The demo uses gradients, anti-aliased primitives, embedded text, alpha sprite
composition, scaling, tinting, and transformed blits. The binding is typed and
ships a `py.typed` marker.

## Versioning

The Python package and C ABI are versioned independently:

```python
soft_raster.__version__       # binding version, currently 0.1.0
soft_raster.SOFT_RASTER_ABI   # minimum native API line, currently (0, 3)
```

The C library predates a runtime version-query symbol, so compatibility is
validated by its complete required symbol set. A missing symbol raises
`IncompatibleLibraryError` at load time.

## License

MIT. The native `soft-raster` library in the parent directory is also MIT
licensed and carries its own embedded-font provenance notice.
