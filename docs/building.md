# Building, testing and installing soft-raster

Build, test, benchmark and install instructions, moved here from the README.

## Build and test

```sh
make
make test
make sanitize
make benchmark
./build/demo
```

The final command renders a composed scene (gradient sky, anti-aliased
circles and rings, dashed lines, outlined text, sprite blits, letterbox
scaling) into `demo.ppm`. The test suite asserts exact pixel values for
clipped stores, blend fixed-point math, edge coverage of rectangles,
circles, rings, and dashed lines, glyph bits against the font table, blit
clipping at all four edges, scaler geometry, and the PPM writer.

The typed, pure-Python binding is maintained in [`python/`](../python). It uses
the same shared library without bundling a second native implementation:

```sh
make python-check
make python-wheel
make python-benchmark
```

The wheel is written below `python/dist/`.

## Install

```sh
make install PREFIX=/usr/local
```

The build produces static and shared libraries. Applications may instead
compile `src/soft_raster.c` directly. The library depends only on the C
standard library and `libm`; nothing in it requires POSIX.

The API is pre-1.0 and may change between minor releases. Version 0.4.0 adds
direct RGB packing and selectable font APIs without changing existing calls.
