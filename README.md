# soft-raster

`soft-raster` is a small C11 software rasterizer that draws anti-aliased
shapes, bitmap text and alpha sprites into 32-bit framebuffers.

It is not installed on its own. Kilix games and terminal apps pin it as a Git
submodule (most of them through
[kilix-game-sdk](https://github.com/itsmygithubacct/kilix-game-sdk#readme))
and compile it into their binaries; those games and apps install on demand,
from the Kilix content catalog or the first time a Kilix command needs them.

## What it does

- Draws anti-aliased rectangles, circles, ellipses, rings and dashed lines, and
  hard-edged triangles and convex or concave polygons, all clipped to a canvas
  clip rectangle.
- Strokes poly-lines as one shape (round joins, chosen end caps, a dash phase
  along the whole path), fills rounded rectangles and anti-aliased polygons, and
  flattens cubic Bézier curves: the primitives a graph or diagram needs.
- Renders text in an embedded 8x16 console font or a compact 7x14 face, with
  centred, outlined and shadowed variants.
- Composites premultiplied-alpha sprites (plain, tinted, scaled, flipped or
  transposed) and letterbox-scales a whole canvas onto another.
- Loads and writes binary P6 PPM files for small assets and golden-image tests.
- Depends only on the C standard library and `libm`; nothing requires POSIX.
- Ships a typed pure-Python binding over the same shared library in [`python/`](python/).

## Quick start

```sh
git clone https://github.com/itsmygithubacct/soft-raster.git
cd soft-raster
make            # build/libsoft-raster.a, build/libsoft-raster.so, build/demo
make test       # exact-pixel test suites
./build/demo    # writes demo.ppm
```

```c
#include "soft_raster.h"

sr_canvas frame;
if (sr_canvas_init(&frame, 640, 360)) {
    sr_clear(&frame, 0x101018);
    sr_fill_circle(&frame, 320.0f, 180.0f, 48.0f, 0xffd166, 1.0f);
    sr_text_center(&frame, 320.0f, 24.0f, "HELLO", 0xf8fafc, 1.0f, 2);
    sr_write_ppm(&frame, "frame.ppm");
    sr_canvas_free(&frame);
}
```

An application can also compile `src/soft_raster.c` directly instead of linking
the library.

## Usage

| Area | Main calls |
|---|---|
| Canvas | `sr_canvas_init`, `sr_canvas_wrap`, `sr_canvas_free`, `sr_canvas_set_clip`, `sr_canvas_reset_clip` |
| Pixels and packing | `sr_clear`, `sr_px`, `sr_blend`, `sr_pack_rgba`, `sr_pack_rgb` |
| Shapes | `sr_fill_rect`, `sr_stroke_rect`, `sr_fill_circle`, `sr_fill_ellipse`, `sr_ring`, `sr_line`, `sr_fill_triangle`, `sr_fill_convex`, `sr_fill_polygon` |
| Graph primitives | `sr_polyline`, `sr_fill_polygon_aa`, `sr_fill_round_rect`, `sr_stroke_round_rect`, `sr_flatten_cubic` |
| Text | `sr_text`, `sr_text_center`, `sr_text_outlined`, `sr_text_shadow`, the `sr_*_in` face-selecting variants, `sr_text_width_in` |
| Blits and scaling | `sr_blit`, `sr_blit_alpha`, `sr_blit_tint`, `sr_blit_scaled`, `sr_blit_transformed`, `sr_scale_canvas` |
| Files | `sr_load_ppm`, `sr_write_ppm` |

| Make target | What it does |
|---|---|
| `make` | static and shared libraries plus the demo |
| `make test` | the raster and graph-primitive test suites |
| `make sanitize` | both suites under AddressSanitizer and UndefinedBehaviorSanitizer |
| `make benchmark` | the C microbenchmark |
| `make python-check`, `make python-wheel`, `make python-benchmark` | the Python binding's checks, wheel and benchmark |
| `make install PREFIX=/usr/local` | installs the header and both libraries |

## How it works

A canvas is a row-major array of `uint32_t` pixels in `0xAARRGGBB` order,
either allocated by `sr_canvas_init()` or wrapped around memory the caller
already owns. Drawing calls take float pixel coordinates; except in the
hard-edged triangle and polygon fills, which sample pixel centres, an edge that
falls between pixel centres gets fractional coverage, which is quantised to 1/256
steps and blended toward the colour. Drawing on a transparent canvas therefore
builds a premultiplied-alpha sprite, which the blit calls composite over another
canvas.

The code was extracted from the renderer the terminal-lander family of games
used to duplicate in every game, with the blending maths kept identical, so
frames match those games byte for byte on an opaque canvas. Ordinary drawing
uses only canvas and stack memory. Canvas creation allocates, and so do large
shapes: polygons above 64 vertices, poly-lines above 128 points, and
anti-aliased polygon fills or poly-lines whose clipped bounds are wider than
4096 pixels take a temporary buffer. The PPM routines are the only ones that
touch the file system.

Many Kilix games draw each frame into a canvas, pack it with
`sr_pack_rgba()`, and hand it to a presenter such as
[kitty-framebuffer](https://github.com/itsmygithubacct/kitty-framebuffer#readme),
which sends it to the terminal over the Kitty graphics protocol.

The full API, with the pixel-format and overlap rules, is in
[docs/api.md](docs/api.md).

## Documentation

- [docs/api.md](docs/api.md): the C API reference (pixel format, canvas,
  drawing, graph primitives, text, blits and scaling, PPM images).
- [docs/building.md](docs/building.md): build, test, sanitizer, benchmark,
  Python and install targets.
- [python/README.md](python/README.md): the `soft-raster-py` Python binding.
- [CHANGELOG.md](CHANGELOG.md): release history.

## Related repositories

Projects that pin or vendor `soft-raster`:

- [kilix-game-sdk](https://github.com/itsmygithubacct/kilix-game-sdk#readme)
  pins it at `kilix-game-kit/third_party/soft-raster`; its renderers and
  `kilix-ui` reuse that checkout. The Kitty-graphics games built on the SDK
  draw through it: the graphical games in [kilix-games](https://github.com/itsmygithubacct/kilix-games#readme),
  [pleb-tower](https://github.com/itsmygithubacct/pleb-tower#readme),
  [super-kilix](https://github.com/itsmygithubacct/super-kilix#readme),
  [pleb-plant-grower](https://github.com/itsmygithubacct/pleb-plant-grower#readme)
  and [kilix-land](https://github.com/itsmygithubacct/kilix-land#readme).
- [kilix-techno](https://github.com/itsmygithubacct/kilix-techno#readme),
  [kilix-playalong](https://github.com/itsmygithubacct/kilix-playalong#readme),
  [kilix-rtsp](https://github.com/itsmygithubacct/kilix-rtsp#readme),
  [kilix-sound-detect](https://github.com/itsmygithubacct/kilix-sound-detect#readme)
  and [kilix-mask](https://github.com/itsmygithubacct/kilix-mask#readme) pin it
  directly at `third_party/soft-raster`.
- [kilix-graphs](https://github.com/itsmygithubacct/kilix-graphs#readme) uses
  the Python binding for its optional pixel backend.
- [bash-os](https://github.com/itsmygithubacct/bash-os#readme) carries a
  vendored copy under `loadables/_soft_raster/`.

Frames drawn with it are usually presented by
[kitty-framebuffer](https://github.com/itsmygithubacct/kitty-framebuffer#readme)
through [kitty-terminal-session](https://github.com/itsmygithubacct/kitty-terminal-session#readme).

## Part of Plebian-OS

soft-raster is one of the repositories behind
[Plebian-OS](https://github.com/itsmygithubacct/plebian-os#readme), a Debian
install whose desktop is [Kilix](https://github.com/itsmygithubacct/kilix#readme)
running the [Kilix 95](https://github.com/itsmygithubacct/kilix-95#readme)
desktop through [Pleb](https://github.com/itsmygithubacct/pleb#readme).
It reaches a user compiled into the Kilix games and apps that pin it, which
install on demand from the Kilix content catalog or on first use of a Kilix
command. The map of every repository and how it ships is on the
[components page](https://github.com/itsmygithubacct/plebian-os/blob/main/docs/components.md).

## Font

`src/font8x16.h` holds 8x16 bitmaps for ASCII 32..126, extracted from
`Lat15-Fixed16` as shipped in Debian's console-setup package, whose
copyright file states that all console fonts are public domain by nature.
The Fixed typeface descends from the public-domain X11 misc-fixed fonts;
out of caution, the permissive notice from that lineage is preserved in the
header.

## License

MIT. See [LICENSE](LICENSE) for the complete notices, including the font
provenance note.
