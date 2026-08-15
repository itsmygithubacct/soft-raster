#include "soft_raster.h"

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            (void)fprintf(                                                    \
                stderr,                                                       \
                "%s:%d: check failed: %s\n",                               \
                __FILE__,                                                     \
                __LINE__,                                                     \
                #condition);                                                  \
            return false;                                                     \
        }                                                                     \
    } while (false)

/* Red channel of a canvas pixel; the reference values below were captured
 * from the renderer this library was extracted from, which stores one byte
 * per channel. */
static int red_at(const sr_canvas *c, int x, int y)
{
    return (int)((c->px[(size_t)y * (size_t)c->w + (size_t)x] >> 16) & 255u);
}

static uint32_t px_at(const sr_canvas *c, int x, int y)
{
    return c->px[(size_t)y * (size_t)c->w + (size_t)x];
}

static bool
test_canvas_lifecycle_and_overflow(void)
{
    sr_canvas c;

    CHECK(sr_canvas_init(&c, 4, 3));
    CHECK(c.px != NULL && c.w == 4 && c.h == 3 && c.owns_px);
    for (int i = 0; i < 12; i++) {
        CHECK(c.px[i] == 0x00000000u);  /* starts fully transparent */
    }
    sr_canvas_free(&c);
    CHECK(c.px == NULL && c.w == 0 && c.h == 0);

    CHECK(!sr_canvas_init(&c, 0, 8));
    CHECK(!sr_canvas_init(&c, 8, -1));
    CHECK(!sr_canvas_init(&c, INT_MAX, INT_MAX));  /* w*h overflow */
    CHECK(!sr_canvas_init(&c, 65536, 65536));      /* pixel count > INT_MAX */
    CHECK(c.px == NULL && c.w == 0 && c.h == 0);
    sr_canvas_free(&c);  /* freeing a failed canvas is harmless */
    return true;
}

static bool
test_wrap_does_not_free_caller_memory(void)
{
    uint32_t mem[4 * 4];
    sr_canvas c;

    for (int i = 0; i < 16; i++)
        mem[i] = 0xdeadbeefu;
    sr_canvas_wrap(&c, mem, 4, 4);
    CHECK(c.px == mem && c.w == 4 && c.h == 4 && !c.owns_px);
    CHECK(px_at(&c, 1, 1) == 0xdeadbeefu);  /* wrap neither copies nor clears */

    sr_px(&c, 0, 0, 0x123456u);
    sr_canvas_free(&c);
    CHECK(c.px == NULL);
    CHECK(mem[0] == 0xff123456u);  /* caller memory intact after free */
    CHECK(mem[15] == 0xdeadbeefu);

    sr_canvas_wrap(&c, mem, INT_MAX, INT_MAX);
    CHECK(c.px == NULL && c.w == 0 && c.h == 0 && !c.owns_px);
    CHECK(c.clip_x0 == 0 && c.clip_y0 == 0 &&
          c.clip_x1 == 0 && c.clip_y1 == 0);
    return true;
}

static bool
test_clip_and_rgba_pack(void)
{
    sr_canvas c;
    uint8_t rgba[4u * 4u * 4u] = {0};
    uint8_t rgb[4u * 4u * 3u] = {0};

    CHECK(sr_canvas_init(&c, 4, 4));
    sr_clear(&c, 0x102030u);
    sr_canvas_set_clip(&c, 1, 1, 2, 2);
    sr_px(&c, 0, 0, 0xffffffu);
    sr_fill_rect(&c, 0.0f, 0.0f, 4.0f, 4.0f, 0xa0b0c0u, 1.0f);
    CHECK(px_at(&c, 0, 0) == 0xff102030u);
    CHECK(px_at(&c, 1, 1) == 0xffa0b0c0u);
    CHECK(px_at(&c, 3, 3) == 0xff102030u);
    sr_canvas_reset_clip(&c);
    sr_px(&c, 0, 0, 0x010203u);
    CHECK(sr_pack_rgba(&c, rgba, sizeof rgba));
    CHECK(rgba[0] == 1u && rgba[1] == 2u && rgba[2] == 3u && rgba[3] == 255u);
    CHECK(!sr_pack_rgba(&c, rgba, sizeof rgba - 1u));
    CHECK(sr_pack_rgb(&c, rgb, sizeof rgb));
    CHECK(rgb[0] == 1u && rgb[1] == 2u && rgb[2] == 3u);
    CHECK(rgb[3] == 0x10u && rgb[4] == 0x20u && rgb[5] == 0x30u);
    CHECK(!sr_pack_rgb(&c, rgb, sizeof rgb - 1u));
    sr_canvas_free(&c);
    return true;
}

static bool
test_nonfinite_overflow_and_degenerate_inputs(void)
{
    sr_canvas c;
    const float invalid_xs[3] = {0.0f, INFINITY, 1.0f};
    const float valid_ys[3] = {0.0f, 1.0f, 1.0f};

    CHECK(sr_mix(0x123456u, 0xffffffu, NAN) == 0x123456u);
    CHECK(sr_scale_rgb(0xffffffu, NAN) == 0u);
    CHECK(sr_text_width("A", INT_MAX) == INT_MAX);
    CHECK(sr_text_width_in(SR_FONT_FIXED_8X16, "A", INT_MAX) == INT_MAX);

    CHECK(sr_canvas_init(&c, 8, 8));
    sr_clear(&c, 0x010203u);
    sr_blend(&c, 0, 0, 0xffffffu, NAN);
    sr_fill_rect(&c, INFINITY, 0.0f, 1.0f, 1.0f,
                 0xffffffu, 1.0f);
    sr_fill_circle(&c, 4.0f, 4.0f, NAN, 0xffffffu, 1.0f);
    sr_fill_ellipse(&c, 4.0f, 4.0f, INFINITY, 2.0f,
                    0xffffffu, 1.0f);
    sr_ring(&c, 4.0f, 4.0f, 2.0f, NAN, 0xffffffu, 1.0f);
    sr_text(&c, NAN, 0.0f, "A", 0xffffffu, 1.0f, 1);
    sr_fill_triangle(&c, 1.0f, 1.0f, 2.0f, 2.0f, 3.0f, 3.0f,
                     0xffffffu, 1.0f);
    sr_fill_polygon(&c, invalid_xs, valid_ys, 3u, 0xffffffu, 1.0f);
    sr_fill_polygon(&c, invalid_xs, valid_ys,
                    SIZE_MAX / sizeof(double) + 1u, 0xffffffu, 1.0f);
    sr_text(&c, -INFINITY, 0.0f, "invisible", 0xffffffu, 1.0f, 1);
    sr_text(&c, -FLT_MAX, 0.0f, "invisible", 0xffffffu, 1.0f, 1);
    sr_line(&c, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0x010203u, 1.0f,
            INT_MAX, INT_MAX);
    CHECK(px_at(&c, 0, 0) == 0xff010203u);
    CHECK(px_at(&c, 4, 4) == 0xff010203u);
    sr_canvas_free(&c);
    return true;
}

static bool
test_clipped_pixel_stores(void)
{
    sr_canvas c;

    CHECK(sr_canvas_init(&c, 4, 4));
    sr_clear(&c, 0x101010u);
    CHECK(px_at(&c, 3, 3) == 0xff101010u);

    sr_px(&c, 2, 1, 0xaabbccu);
    CHECK(px_at(&c, 2, 1) == 0xffaabbccu);  /* opaque store */

    sr_px(&c, -1, 0, 0xffffffu);
    sr_px(&c, 4, 0, 0xffffffu);
    sr_px(&c, 0, -1, 0xffffffu);
    sr_px(&c, 0, 4, 0xffffffu);
    sr_blend(&c, -1, -1, 0xffffffu, 1.0f);
    CHECK(px_at(&c, 0, 0) == 0xff101010u);  /* edges untouched */
    CHECK(px_at(&c, 3, 0) == 0xff101010u);
    CHECK(px_at(&c, 0, 3) == 0xff101010u);

    sr_canvas_free(&c);
    return true;
}

static bool
test_blend_math(void)
{
    sr_canvas c;

    CHECK(sr_canvas_init(&c, 4, 4));
    sr_clear(&c, 0x000000u);

    /* alpha 0.5 -> coverage 128/256: 0 + ((255 - 0) * 128 >> 8) = 127 */
    sr_blend(&c, 0, 0, 0xffffffu, 0.5f);
    CHECK(px_at(&c, 0, 0) == 0xff7f7f7fu);

    /* alpha 0.25 over red 64 toward 200: 64 + ((200 - 64) * 64 >> 8) = 98 */
    sr_px(&c, 1, 0, 0x400000u);
    sr_blend(&c, 1, 0, 0xc80000u, 0.25f);
    CHECK(red_at(&c, 1, 0) == 98);

    /* alpha 1 lands exactly on the color; alpha 0 is a no-op */
    sr_blend(&c, 2, 0, 0x315263u, 1.0f);
    CHECK(px_at(&c, 2, 0) == 0xff315263u);
    sr_blend(&c, 2, 0, 0xffffffu, 0.0f);
    CHECK(px_at(&c, 2, 0) == 0xff315263u);
    sr_canvas_free(&c);

    /* on a transparent canvas, blending accumulates coverage in the alpha
     * byte and leaves RGB premultiplied by it */
    CHECK(sr_canvas_init(&c, 2, 2));
    sr_blend(&c, 0, 0, 0xff0000u, 0.5f);
    CHECK(px_at(&c, 0, 0) == 0x7f7f0000u);
    sr_canvas_free(&c);
    return true;
}

static bool
test_color_helpers(void)
{
    CHECK(sr_rgb(0x01, 0x02, 0x03) == 0x010203u);
    CHECK(sr_rgb(255, 255, 255) == 0xffffffu);
    CHECK(sr_mix(0x000000u, 0xffffffu, 0.5f) == 0x7f7f7fu);
    CHECK(sr_mix(0x204060u, 0x204060u, 0.3f) == 0x204060u);
    CHECK(sr_mix(0x000000u, 0xffffffu, -1.0f) == 0x000000u);  /* t clamped */
    CHECK(sr_mix(0x000000u, 0xffffffu, 2.0f) == 0xffffffu);
    CHECK(sr_scale_rgb(0x404040u, 0.5f) == 0x202020u);
    CHECK(sr_scale_rgb(0x808080u, 2.0f) == 0xffffffu);  /* saturates */
    CHECK(sr_scale_rgb(0x102030u, 0.0f) == 0x000000u);
    return true;
}

static bool
test_fill_rect_edge_coverage(void)
{
    sr_canvas c;

    CHECK(sr_canvas_init(&c, 8, 8));
    sr_clear(&c, 0x000000u);
    sr_fill_rect(&c, 1.5f, 1.5f, 2.0f, 2.0f, 0xffffffu, 1.0f);

    /* reference values from the original renderer: quarter-covered corners,
     * half-covered edges, solid interior */
    CHECK(red_at(&c, 1, 1) == 63);
    CHECK(red_at(&c, 3, 1) == 63);
    CHECK(red_at(&c, 1, 3) == 63);
    CHECK(red_at(&c, 3, 3) == 63);
    CHECK(red_at(&c, 2, 1) == 127);
    CHECK(red_at(&c, 1, 2) == 127);
    CHECK(red_at(&c, 2, 3) == 127);
    CHECK(red_at(&c, 2, 2) == 255);
    CHECK(red_at(&c, 0, 0) == 0);
    CHECK(red_at(&c, 4, 4) == 0);

    /* degenerate sizes draw nothing */
    sr_fill_rect(&c, 5.0f, 5.0f, 0.0f, 4.0f, 0xffffffu, 1.0f);
    CHECK(red_at(&c, 5, 5) == 0);
    sr_canvas_free(&c);
    return true;
}

static bool
test_fill_circle_rim_coverage(void)
{
    sr_canvas c;

    CHECK(sr_canvas_init(&c, 16, 16));
    sr_clear(&c, 0x000000u);
    sr_fill_circle(&c, 8.0f, 8.0f, 4.0f, 0xffffffu, 1.0f);

    /* reference values from the original renderer */
    CHECK(red_at(&c, 8, 8) == 255);    /* center */
    CHECK(red_at(&c, 11, 8) == 246);   /* anti-aliased rim */
    CHECK(red_at(&c, 12, 8) == 0);     /* just outside */
    CHECK(red_at(&c, 10, 10) == 246);  /* diagonal rim */
    CHECK(red_at(&c, 11, 10) == 50);   /* rim falloff */
    CHECK(red_at(&c, 11, 11) == 0);
    CHECK(red_at(&c, 4, 8) == red_at(&c, 11, 8));  /* symmetric */
    sr_canvas_free(&c);
    return true;
}

static bool
test_ring_coverage(void)
{
    sr_canvas c;

    CHECK(sr_canvas_init(&c, 16, 16));
    sr_clear(&c, 0x000000u);
    sr_ring(&c, 8.0f, 8.0f, 5.0f, 2.0f, 0xffffffu, 1.0f);

    /* reference values from the original renderer */
    CHECK(red_at(&c, 13, 8) == 249);  /* outer stroke */
    CHECK(red_at(&c, 12, 8) == 255);  /* stroke core */
    CHECK(red_at(&c, 11, 8) == 8);    /* inner falloff */
    CHECK(red_at(&c, 14, 8) == 0);    /* outside */
    CHECK(red_at(&c, 8, 3) == 255);   /* top of the stroke */
    CHECK(red_at(&c, 8, 8) == 0);     /* hollow center */
    sr_canvas_free(&c);
    return true;
}

static bool
test_line_width_dash_and_coverage(void)
{
    static const int dashed_row4[20] = {
        0, 255, 255, 255, 255, 255, 0, 0, 0, 255,
        255, 255, 255, 0, 0, 0, 255, 0, 0, 0
    };
    static const int dashed_row3[20] = {
        0, 97, 127, 127, 127, 127, 0, 0, 0, 127,
        127, 127, 127, 0, 0, 0, 97, 0, 0, 0
    };
    sr_canvas c;

    CHECK(sr_canvas_init(&c, 20, 10));

    /* reference rows from the original renderer: width 2 centered on
     * y = 4.5 gives a solid center row and half-covered rows above and
     * below; dash 4-on 3-off gates pixels by distance along the line */
    sr_clear(&c, 0x000000u);
    sr_line(&c, 2.0f, 4.5f, 16.0f, 4.5f, 2.0f, 0xffffffu, 1.0f, 4, 3);
    for (int x = 0; x < 20; x++) {
        CHECK(red_at(&c, x, 4) == dashed_row4[x]);
        CHECK(red_at(&c, x, 3) == dashed_row3[x]);
        CHECK(red_at(&c, x, 5) == dashed_row3[x]);
    }

    /* dash 0/0 is solid */
    sr_clear(&c, 0x000000u);
    sr_line(&c, 2.0f, 4.5f, 16.0f, 4.5f, 2.0f, 0xffffffu, 1.0f, 0, 0);
    for (int x = 1; x <= 16; x++)
        CHECK(red_at(&c, x, 4) == 255);
    CHECK(red_at(&c, 0, 4) == 0);
    CHECK(red_at(&c, 17, 4) == 0);
    CHECK(red_at(&c, 2, 3) == 127);
    sr_canvas_free(&c);
    return true;
}

static bool
test_fill_triangle(void)
{
    sr_canvas c;

    CHECK(sr_canvas_init(&c, 16, 16));
    sr_clear(&c, 0x000000u);
    sr_fill_triangle(&c, 1.0f, 1.0f, 13.0f, 1.0f, 1.0f, 13.0f,
                     0xffffffu, 1.0f);
    CHECK(red_at(&c, 3, 3) == 255);   /* inside */
    CHECK(red_at(&c, 2, 10) == 255);  /* near the vertical edge */
    CHECK(red_at(&c, 12, 12) == 0);   /* beyond the hypotenuse */
    CHECK(red_at(&c, 14, 2) == 0);    /* outside */

    /* opposite winding fills too */
    sr_clear(&c, 0x000000u);
    sr_fill_triangle(&c, 1.0f, 13.0f, 13.0f, 1.0f, 1.0f, 1.0f,
                     0xffffffu, 1.0f);
    CHECK(red_at(&c, 3, 3) == 255);
    sr_canvas_free(&c);
    return true;
}

static bool
test_ellipse_and_convex_fill(void)
{
    sr_canvas c;
    static const float xs[] = {2.0f, 12.0f, 10.0f, 4.0f};
    static const float ys[] = {2.0f, 3.0f, 12.0f, 11.0f};

    CHECK(sr_canvas_init(&c, 16, 16));
    sr_clear(&c, 0x000000u);
    sr_fill_ellipse(&c, 8.0f, 8.0f, 6.0f, 3.0f, 0xffffffu, 1.0f);
    CHECK(red_at(&c, 8, 8) == 255);
    CHECK(red_at(&c, 13, 8) > 0);
    CHECK(red_at(&c, 8, 11) == 0);
    CHECK(red_at(&c, 0, 8) == 0);

    sr_clear(&c, 0x000000u);
    sr_fill_convex(&c, xs, ys, 4u, 0xffffffu, 1.0f);
    CHECK(red_at(&c, 7, 7) == 255);
    CHECK(red_at(&c, 0, 0) == 0);
    CHECK(red_at(&c, 14, 14) == 0);
    sr_canvas_free(&c);
    return true;
}

/* The case sr_fill_convex() cannot express.
 *
 * Given a concave outline it draws the intersection of the edges'
 * half-planes -- a smaller shape than asked for.  For this L that is the
 * 4x4 block where the arms overlap, and both arms disappear.  The test
 * asserts that difference in both directions, so it fails if the two
 * functions are ever accidentally made equivalent. */
static bool
test_fill_polygon_concave(void)
{
    /* (2,2) (12,2) (12,6) (6,6) (6,12) (2,12) -- an L with a horizontal
     * arm along the top and a vertical arm down the left. */
    static const float xs[6] = {2.0f, 12.0f, 12.0f, 6.0f, 6.0f, 2.0f};
    static const float ys[6] = {2.0f, 2.0f, 6.0f, 6.0f, 12.0f, 12.0f};
    sr_canvas c;

    CHECK(sr_canvas_init(&c, 16, 16));
    sr_fill_polygon(&c, xs, ys, 6u, 0xffffffu, 1.0f);
    CHECK(red_at(&c, 4, 4) == 255);    /* corner where the arms meet */
    CHECK(red_at(&c, 9, 4) == 255);    /* far end of the horizontal arm */
    CHECK(red_at(&c, 4, 9) == 255);    /* far end of the vertical arm */
    CHECK(red_at(&c, 9, 9) == 0);      /* the notch stays empty */
    CHECK(red_at(&c, 11, 11) == 0);
    CHECK(red_at(&c, 0, 0) == 0);      /* outside the bounding box */
    CHECK(red_at(&c, 15, 15) == 0);
    sr_canvas_free(&c);

    /* The same outline through sr_fill_convex keeps only the overlap and
     * loses both arms, which is why this function exists. */
    CHECK(sr_canvas_init(&c, 16, 16));
    sr_fill_convex(&c, xs, ys, 6u, 0xffffffu, 1.0f);
    CHECK(red_at(&c, 4, 4) == 255);    /* overlap survives */
    CHECK(red_at(&c, 9, 4) == 0);      /* horizontal arm lost */
    CHECK(red_at(&c, 4, 9) == 0);      /* vertical arm lost */
    CHECK(red_at(&c, 9, 9) == 0);      /* it under-fills, never over-fills */
    sr_canvas_free(&c);
    return true;
}

/* Convex input must land in the same place through either function, or
 * callers cannot migrate between them.  The offsets keep every edge off
 * the pixel centers, where the two boundary rules legitimately differ --
 * that difference is its own test below. */
static bool
test_fill_polygon_matches_convex(void)
{
    static const float xs[4] = {3.2f, 12.7f, 11.8f, 4.3f};
    static const float ys[4] = {3.4f, 4.2f, 12.6f, 11.9f};
    sr_canvas poly, conv;
    int x, y, differing = 0;

    CHECK(sr_canvas_init(&poly, 16, 16));
    CHECK(sr_canvas_init(&conv, 16, 16));
    sr_fill_polygon(&poly, xs, ys, 4u, 0xffffffu, 1.0f);
    sr_fill_convex(&conv, xs, ys, 4u, 0xffffffu, 1.0f);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            if (px_at(&poly, x, y) != px_at(&conv, x, y)) differing++;
    CHECK(differing == 0);
    sr_canvas_free(&poly);
    sr_canvas_free(&conv);
    return true;
}

/* Half-open spans: a shared edge belongs to exactly one of two abutting
 * polygons.  Filled at half alpha, a seam would show as a doubled blend
 * along the join and a gap would show as background. */
static bool
test_fill_polygon_tiles_without_seams(void)
{
    static const float ax[4] = {0.0f, 8.0f, 8.0f, 0.0f};
    static const float ay[4] = {0.0f, 0.0f, 12.0f, 12.0f};
    static const float bx[4] = {8.0f, 16.0f, 16.0f, 8.0f};
    static const float by[4] = {0.0f, 0.0f, 12.0f, 12.0f};
    sr_canvas c;
    int x;

    CHECK(sr_canvas_init(&c, 16, 12));
    sr_clear(&c, 0x000000u);
    sr_fill_polygon(&c, ax, ay, 4u, 0xffffffu, 0.5f);
    sr_fill_polygon(&c, bx, by, 4u, 0xffffffu, 0.5f);
    /* every pixel across the join carries exactly one blend: a seam would
     * read higher from a doubled blend, a gap would read 0 */
    for (x = 0; x < 16; x++)
        CHECK(red_at(&c, x, 6) == 127);
    sr_canvas_free(&c);
    return true;
}

static bool
test_fill_polygon_winding_and_selfintersection(void)
{
    static const float xs[6] = {2.0f, 12.0f, 12.0f, 6.0f, 6.0f, 2.0f};
    static const float ys[6] = {2.0f, 2.0f, 6.0f, 6.0f, 12.0f, 12.0f};
    float rx[6], ry[6];
    sr_canvas a, b;
    int x, y, differing = 0;
    size_t i;

    /* Reversed winding must render identically: hand-authored coordinates
     * should not have to be ordered. */
    for (i = 0u; i < 6u; i++) {
        rx[i] = xs[5u - i];
        ry[i] = ys[5u - i];
    }
    CHECK(sr_canvas_init(&a, 16, 16));
    CHECK(sr_canvas_init(&b, 16, 16));
    sr_fill_polygon(&a, xs, ys, 6u, 0xffffffu, 1.0f);
    sr_fill_polygon(&b, rx, ry, 6u, 0xffffffu, 1.0f);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            if (px_at(&a, x, y) != px_at(&b, x, y)) differing++;
    CHECK(differing == 0);
    sr_canvas_free(&a);
    sr_canvas_free(&b);

    /* Even-odd: a bowtie's two lobes fill and the crossing point does not
     * become a filled bridge between them. */
    {
        static const float bx[4] = {2.0f, 14.0f, 2.0f, 14.0f};
        static const float by[4] = {2.0f, 14.0f, 14.0f, 2.0f};
        sr_canvas c;

        CHECK(sr_canvas_init(&c, 16, 16));
        sr_fill_polygon(&c, bx, by, 4u, 0xffffffu, 1.0f);
        CHECK(red_at(&c, 8, 4) == 255);    /* upper lobe */
        CHECK(red_at(&c, 8, 11) == 255);   /* lower lobe */
        CHECK(red_at(&c, 3, 8) == 0);      /* left gap between lobes */
        CHECK(red_at(&c, 13, 8) == 0);     /* right gap */
        sr_canvas_free(&c);
    }
    return true;
}

static bool
test_fill_polygon_clipping_and_rejects(void)
{
    static const float xs[4] = {-6.0f, 10.0f, 10.0f, -6.0f};
    static const float ys[4] = {-6.0f, -6.0f, 10.0f, 10.0f};
    static const float tiny[2] = {1.0f, 2.0f};
    sr_canvas c;

    /* A polygon starting off-canvas must still fill what is on it. */
    CHECK(sr_canvas_init(&c, 16, 16));
    sr_fill_polygon(&c, xs, ys, 4u, 0xffffffu, 1.0f);
    CHECK(red_at(&c, 0, 0) == 255);
    CHECK(red_at(&c, 9, 9) == 255);
    CHECK(red_at(&c, 12, 12) == 0);

    /* and the clip rectangle must still bound it */
    sr_canvas_set_clip(&c, 4, 4, 3, 3);
    sr_fill_polygon(&c, xs, ys, 4u, 0xff0000u, 1.0f);
    CHECK(red_at(&c, 5, 5) == 255);
    sr_canvas_reset_clip(&c);

    /* degenerate input draws nothing and does not crash */
    sr_fill_polygon(&c, xs, ys, 2u, 0x00ff00u, 1.0f);
    sr_fill_polygon(&c, NULL, ys, 4u, 0x00ff00u, 1.0f);
    sr_fill_polygon(&c, xs, NULL, 4u, 0x00ff00u, 1.0f);
    sr_fill_polygon(NULL, xs, ys, 4u, 0x00ff00u, 1.0f);
    sr_fill_polygon(&c, tiny, tiny, 2u, 0x00ff00u, 1.0f);
    sr_canvas_free(&c);

    /* more vertices than the stack buffer holds, exercising the heap path */
    {
        float bigx[96], bigy[96];
        size_t i;

        for (i = 0u; i < 96u; i++) {
            float t = (float)i / 96.0f * 6.2831853f;
            bigx[i] = 32.0f + 20.0f * cosf(t);
            bigy[i] = 32.0f + 20.0f * sinf(t);
        }
        CHECK(sr_canvas_init(&c, 64, 64));
        sr_fill_polygon(&c, bigx, bigy, 96u, 0xffffffu, 1.0f);
        CHECK(red_at(&c, 32, 32) == 255);
        CHECK(red_at(&c, 1, 1) == 0);
        sr_canvas_free(&c);
    }
    return true;
}

static bool
test_fill_polygon_alpha(void)
{
    static const float xs[4] = {0.0f, 8.0f, 8.0f, 0.0f};
    static const float ys[4] = {0.0f, 0.0f, 8.0f, 8.0f};
    sr_canvas c;

    CHECK(sr_canvas_init(&c, 8, 8));
    sr_clear(&c, 0x000000u);
    sr_fill_polygon(&c, xs, ys, 4u, 0xffffffu, 0.5f);
    /* half coverage of white over black: 0 + ((255 - 0) * 128 >> 8) = 127,
     * matching the blend-math reference above */
    CHECK(red_at(&c, 4, 4) == 127);
    sr_canvas_free(&c);
    return true;
}

static bool
test_text_metrics_and_glyph_bits(void)
{
    /* the embedded font's 'A', copied from the table: 16 rows, MSB is the
     * leftmost pixel */
    static const unsigned char glyph_a[16] = {
        0x00, 0x00, 0x00, 0x00, 0x18, 0x24, 0x24, 0x42,
        0x42, 0x7e, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00
    };
    sr_canvas c;

    CHECK(sr_text_width("AB", 2) == 2 * SR_FONT_W * 2);
    CHECK(sr_text_width("", 1) == 0);
    CHECK(sr_text_width("A", 0) == SR_FONT_W);  /* scale clamps to 1 */
    CHECK(memcmp(sr_font_glyph('A'), glyph_a, sizeof glyph_a) == 0);
    CHECK(memcmp(sr_font_glyph(1u), sr_font_glyph('?'), SR_FONT_H) == 0);

    CHECK(sr_canvas_init(&c, 16, 20));
    sr_clear(&c, 0x000000u);
    sr_text(&c, 0.0f, 0.0f, "A", 0xffffffu, 1.0f, 1);
    for (int gy = 0; gy < SR_FONT_H; gy++)
        for (int gx = 0; gx < SR_FONT_W; gx++) {
            int on = (glyph_a[gy] >> (7 - gx)) & 1;
            CHECK(red_at(&c, gx, gy) == (on ? 255 : 0));
        }

    /* characters outside ASCII 32..126 fall back to '?' */
    sr_clear(&c, 0x000000u);
    sr_text(&c, 0.0f, 0.0f, "\x01", 0xffffffu, 1.0f, 1);
    sr_canvas c2;
    CHECK(sr_canvas_init(&c2, 16, 20));
    sr_clear(&c2, 0x000000u);
    sr_text(&c2, 0.0f, 0.0f, "?", 0xffffffu, 1.0f, 1);
    CHECK(memcmp(c.px, c2.px, (size_t)16 * 20 * sizeof(uint32_t)) == 0);
    sr_canvas_free(&c2);

    /* scale 2 doubles each glyph pixel */
    sr_clear(&c, 0x000000u);
    sr_text(&c, 0.0f, 0.0f, "A", 0xffffffu, 1.0f, 2);
    CHECK(red_at(&c, 6, 8) == 255);   /* (3,4) scaled */
    CHECK(red_at(&c, 7, 9) == 255);
    CHECK(red_at(&c, 0, 8) == 0);
    sr_canvas_free(&c);
    return true;
}

static bool
test_text_outline_and_shadow(void)
{
    sr_canvas c;

    CHECK(sr_canvas_init(&c, 24, 24));

    /* 'I' row 4 is 0x3e: columns 2..6, so x = 4..8 when drawn at x = 2 */
    sr_clear(&c, 0x202020u);
    sr_text_outlined(&c, 2.0f, 2.0f, "I", 0xffffffu, 1.0f, 1);
    CHECK(px_at(&c, 8, 6) == 0xffffffffu);  /* glyph fill */
    CHECK(px_at(&c, 9, 6) == 0xff000000u);  /* outline just right of it */
    CHECK(px_at(&c, 3, 6) == 0xff000000u);  /* outline just left of it */
    CHECK(px_at(&c, 0, 0) == 0xff202020u);  /* background survives */

    /* shadow at +1,+1 with alpha 0.75: 32 + ((0 - 32) * 192 >> 8) = 8 */
    sr_clear(&c, 0x202020u);
    sr_text_shadow(&c, 2.0f, 2.0f, "I", 0xffffffu, 1.0f, 1);
    CHECK(px_at(&c, 8, 6) == 0xffffffffu);
    CHECK(px_at(&c, 9, 7) == 0xff080808u);

    /* centered text is symmetric about the axis */
    sr_clear(&c, 0x000000u);
    sr_text_center(&c, 12.0f, 4.0f, "I", 0xffffffu, 1.0f, 1);
    CHECK(red_at(&c, 12, 8) == 255);  /* stem lands on the center column */
    sr_canvas_free(&c);
    return true;
}

static bool
test_blit_clipping_all_edges(void)
{
    sr_canvas dst, src;

    CHECK(sr_canvas_init(&src, 4, 4));
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            sr_px(&src, x, y, sr_rgb((uint8_t)(x * 16), (uint8_t)(y * 16), 9));

    CHECK(sr_canvas_init(&dst, 8, 8));

    /* fully inside: verbatim copy, alpha byte included */
    sr_clear(&dst, 0x000000u);
    sr_blit(&dst, &src, 2, 2);
    CHECK(px_at(&dst, 2, 2) == px_at(&src, 0, 0));
    CHECK(px_at(&dst, 5, 5) == px_at(&src, 3, 3));
    CHECK(px_at(&dst, 1, 2) == 0xff000000u);
    CHECK(px_at(&dst, 6, 5) == 0xff000000u);

    /* top-left, top-right, bottom-left, bottom-right overhangs */
    sr_clear(&dst, 0x000000u);
    sr_blit(&dst, &src, -2, -2);
    CHECK(px_at(&dst, 0, 0) == px_at(&src, 2, 2));
    CHECK(px_at(&dst, 1, 1) == px_at(&src, 3, 3));
    CHECK(px_at(&dst, 2, 2) == 0xff000000u);

    sr_clear(&dst, 0x000000u);
    sr_blit(&dst, &src, 6, -2);
    CHECK(px_at(&dst, 6, 0) == px_at(&src, 0, 2));
    CHECK(px_at(&dst, 7, 1) == px_at(&src, 1, 3));
    CHECK(px_at(&dst, 5, 0) == 0xff000000u);

    sr_clear(&dst, 0x000000u);
    sr_blit(&dst, &src, -2, 6);
    CHECK(px_at(&dst, 0, 6) == px_at(&src, 2, 0));
    CHECK(px_at(&dst, 1, 7) == px_at(&src, 3, 1));

    sr_clear(&dst, 0x000000u);
    sr_blit(&dst, &src, 6, 6);
    CHECK(px_at(&dst, 6, 6) == px_at(&src, 0, 0));
    CHECK(px_at(&dst, 7, 7) == px_at(&src, 1, 1));

    /* entirely off-canvas is a no-op */
    sr_clear(&dst, 0x000000u);
    sr_blit(&dst, &src, -4, 0);
    sr_blit(&dst, &src, 8, 0);
    sr_blit(&dst, &src, 0, -4);
    sr_blit(&dst, &src, 0, 8);
    for (int i = 0; i < 64; i++)
        CHECK(dst.px[i] == 0xff000000u);

    sr_canvas_free(&src);
    sr_canvas_free(&dst);
    return true;
}

static bool
test_overlapping_blit(void)
{
    sr_canvas c;
    sr_canvas expected;
    sr_canvas source;

    CHECK(sr_canvas_init(&c, 4, 3));
    CHECK(sr_canvas_init(&expected, 4, 3));
    CHECK(sr_canvas_init(&source, 4, 3));
    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x)
            sr_px(&c, x, y, (uint32_t)(1 + x + y * c.w));

    sr_blit(&c, &c, 1, 0);
    CHECK(px_at(&c, 0, 0) == 0xff000001u);
    CHECK(px_at(&c, 1, 0) == 0xff000001u);
    CHECK(px_at(&c, 2, 0) == 0xff000002u);
    CHECK(px_at(&c, 3, 0) == 0xff000003u);

    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x)
            sr_px(&c, x, y, (uint32_t)(1 + x + y * c.w));
    sr_blit(&c, &c, 0, 1);
    CHECK(px_at(&c, 0, 1) == 0xff000001u);
    CHECK(px_at(&c, 3, 1) == 0xff000004u);
    CHECK(px_at(&c, 0, 2) == 0xff000005u);
    CHECK(px_at(&c, 3, 2) == 0xff000008u);

    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x)
            sr_px(&c, x, y, (uint32_t)(1 + x + y * c.w));
    sr_blit_alpha(&c, &c, 1, 0, 1.0f);
    CHECK(px_at(&c, 1, 0) == 0xff000001u);
    CHECK(px_at(&c, 3, 0) == 0xff000003u);

    for (int y = 0; y < c.h; ++y) {
        for (int x = 0; x < c.w; ++x) {
            const uint32_t value = UINT32_C(0x40102030) +
                                   (uint32_t)(x + y * c.w);
            c.px[(size_t)y * (size_t)c.w + (size_t)x] = value;
            expected.px[(size_t)y * (size_t)c.w + (size_t)x] = value;
            source.px[(size_t)y * (size_t)c.w + (size_t)x] = value;
        }
    }
    sr_blit_alpha(&expected, &source, 1, 1, 0.5f);
    sr_blit_alpha(&c, &c, 1, 1, 0.5f);
    CHECK(memcmp(c.px, expected.px, 12u * sizeof(uint32_t)) == 0);

    memcpy(c.px, source.px, 12u * sizeof(uint32_t));
    memcpy(expected.px, source.px, 12u * sizeof(uint32_t));
    sr_blit_tint(&expected, &source, -1, -1, 0xabcdefu, 0.75f);
    sr_blit_tint(&c, &c, -1, -1, 0xabcdefu, 0.75f);
    CHECK(memcmp(c.px, expected.px, 12u * sizeof(uint32_t)) == 0);

    sr_canvas_free(&source);
    sr_canvas_free(&expected);
    sr_canvas_free(&c);
    return true;
}

static bool
test_blit_alpha_and_tint(void)
{
    sr_canvas dst, spr;

    /* sprite built with draw-into-canvas calls: one opaque pixel and one
     * half-covered pixel over transparency */
    CHECK(sr_canvas_init(&spr, 2, 1));
    sr_px(&spr, 0, 0, 0xff0000u);            /* 0xffff0000 */
    sr_blend(&spr, 1, 0, 0xff0000u, 0.5f);   /* 0x7f7f0000, premultiplied */

    CHECK(sr_canvas_init(&dst, 4, 4));

    /* uniform alpha 1: opaque pixel replaces, half pixel composites */
    sr_clear(&dst, 0xffffffu);
    sr_blit_alpha(&dst, &spr, 1, 1, 1.0f);
    CHECK(px_at(&dst, 1, 1) == 0xffff0000u);
    /* 127 red + 255 * 128/255 white remainder = (255, 128, 128) */
    CHECK(px_at(&dst, 2, 1) == 0xffff8080u);
    CHECK(px_at(&dst, 0, 1) == 0xffffffffu);  /* clipped neighborhood intact */

    /* uniform alpha 0.5 halves the opaque pixel's contribution */
    sr_clear(&dst, 0x0000ffu);
    sr_blit_alpha(&dst, &spr, 1, 1, 0.5f);
    /* ga = 128: red 255*128/255 = 128, blue keeps 255*127/255 = 127 */
    CHECK(px_at(&dst, 1, 1) == 0xff80007fu);

    /* tint replaces color, alpha byte acts as the mask */
    sr_clear(&dst, 0xffffffu);
    sr_blit_tint(&dst, &spr, 1, 1, 0x00ff00u, 1.0f);
    CHECK(px_at(&dst, 1, 1) == 0xff00ff00u);
    /* mask 127: green (255*127 + 255*128)/255 = 255, red 255*128/255 = 128 */
    CHECK(px_at(&dst, 2, 1) == 0xff80ff80u);

    sr_canvas_free(&spr);
    sr_canvas_free(&dst);
    return true;
}

static bool
test_blit_scaled_dimensions(void)
{
    sr_canvas dst, src;

    /* 2x2 checker: left column red, right column green */
    CHECK(sr_canvas_init(&src, 2, 2));
    sr_px(&src, 0, 0, 0xff0000u);
    sr_px(&src, 0, 1, 0xff0000u);
    sr_px(&src, 1, 0, 0x00ff00u);
    sr_px(&src, 1, 1, 0x00ff00u);

    CHECK(sr_canvas_init(&dst, 12, 12));
    sr_clear(&dst, 0x000000u);
    sr_blit_scaled(&dst, &src, 2, 3, 6, 4, 1.0f);

    /* covers exactly x = 2..7, y = 3..6; nearest neighbor puts the source
     * column boundary at destination x = 5 */
    CHECK(px_at(&dst, 2, 3) == 0xffff0000u);
    CHECK(px_at(&dst, 4, 6) == 0xffff0000u);
    CHECK(px_at(&dst, 5, 3) == 0xff00ff00u);
    CHECK(px_at(&dst, 7, 6) == 0xff00ff00u);
    CHECK(px_at(&dst, 1, 3) == 0xff000000u);
    CHECK(px_at(&dst, 8, 3) == 0xff000000u);
    CHECK(px_at(&dst, 2, 2) == 0xff000000u);
    CHECK(px_at(&dst, 2, 7) == 0xff000000u);

    /* clipped scaled blit keeps the same mapping */
    sr_clear(&dst, 0x000000u);
    sr_blit_scaled(&dst, &src, -3, 0, 6, 4, 1.0f);
    CHECK(px_at(&dst, 0, 0) == 0xff00ff00u);  /* dst x 3 of the rect */
    CHECK(px_at(&dst, 3, 0) == 0xff000000u);

    sr_canvas_free(&src);
    sr_canvas_free(&dst);
    return true;
}

static bool
test_blit_transformed_all_combinations(void)
{
    static const uint32_t expected[8][6] = {
        {0xffff0000u, 0xff00ff00u, 0xff0000ffu,
         0xffffff00u, 0xffff00ffu, 0xff00ffffu},
        {0xff0000ffu, 0xff00ff00u, 0xffff0000u,
         0xff00ffffu, 0xffff00ffu, 0xffffff00u},
        {0xffffff00u, 0xffff00ffu, 0xff00ffffu,
         0xffff0000u, 0xff00ff00u, 0xff0000ffu},
        {0xff00ffffu, 0xffff00ffu, 0xffffff00u,
         0xff0000ffu, 0xff00ff00u, 0xffff0000u},
        {0xffff0000u, 0xffffff00u, 0xff00ff00u,
         0xffff00ffu, 0xff0000ffu, 0xff00ffffu},
        {0xffffff00u, 0xffff0000u, 0xffff00ffu,
         0xff00ff00u, 0xff00ffffu, 0xff0000ffu},
        {0xff0000ffu, 0xff00ffffu, 0xff00ff00u,
         0xffff00ffu, 0xffff0000u, 0xffffff00u},
        {0xff00ffffu, 0xff0000ffu, 0xffff00ffu,
         0xff00ff00u, 0xffffff00u, 0xffff0000u}
    };
    static const int widths[8] = {3, 3, 3, 3, 2, 2, 2, 2};
    static const int heights[8] = {2, 2, 2, 2, 3, 3, 3, 3};
    static const uint32_t colors[6] = {
        0xff0000u, 0x00ff00u, 0x0000ffu,
        0xffff00u, 0xff00ffu, 0x00ffffu
    };
    sr_canvas dst, src;

    CHECK(sr_canvas_init(&src, 3, 2));
    CHECK(sr_canvas_init(&dst, 7, 7));
    for (int i = 0; i < 6; i++)
        sr_px(&src, i % 3, i / 3, colors[i]);

    for (uint8_t transform = 0u; transform < 8u; transform++) {
        sr_clear(&dst, 0x101010u);
        sr_blit_transformed(&dst, &src, 2, 2, transform, 1.0f, false,
                            0xabcdefu);
        for (int y = 0; y < dst.h; y++)
            for (int x = 0; x < dst.w; x++) {
                bool inside = x >= 2 && x < 2 + widths[transform] &&
                              y >= 2 && y < 2 + heights[transform];
                if (inside) {
                    int index = (y - 2) * widths[transform] + (x - 2);
                    CHECK(px_at(&dst, x, y) == expected[transform][index]);
                } else {
                    CHECK(px_at(&dst, x, y) == 0xff101010u);
                }
            }
    }

    sr_canvas_free(&src);
    sr_canvas_free(&dst);
    return true;
}

static bool
test_blit_transformed_composition_and_clipping(void)
{
    sr_canvas dst, spr;

    CHECK(sr_canvas_init(&spr, 2, 1));
    sr_px(&spr, 0, 0, 0xff0000u);
    sr_blend(&spr, 1, 0, 0xff0000u, 0.5f);
    CHECK(sr_canvas_init(&dst, 4, 4));

    /* Diagonal exchange turns the 2x1 source into a 1x2 output. */
    sr_clear(&dst, 0x0000ffu);
    sr_blit_transformed(&dst, &spr, 1, 1, SR_TRANSFORM_FLIP_DIAGONAL,
                        0.5f, false, 0x00ff00u);
    CHECK(px_at(&dst, 1, 1) == 0xff80007fu);
    CHECK(px_at(&dst, 1, 2) == 0xff3f00c0u);
    CHECK(px_at(&dst, 2, 1) == 0xff0000ffu);

    sr_clear(&dst, 0xffffffu);
    sr_blit_transformed(&dst, &spr, 1, 1, SR_TRANSFORM_FLIP_DIAGONAL,
                        1.0f, true, 0x00ff00u);
    CHECK(px_at(&dst, 1, 1) == 0xff00ff00u);
    CHECK(px_at(&dst, 1, 2) == 0xff80ff80u);

    /* Alpha clamps above one, while zero, negative, and NaN are no-ops. */
    sr_clear(&dst, 0x000000u);
    sr_blit_transformed(&dst, &spr, 0, 0, 0u, 2.0f, false, 0u);
    CHECK(px_at(&dst, 0, 0) == 0xffff0000u);
    sr_clear(&dst, 0x000000u);
    sr_blit_transformed(&dst, &spr, 0, 0, 0u, 0.0f, false, 0u);
    sr_blit_transformed(&dst, &spr, 0, 0, 0u, -1.0f, false, 0u);
    sr_blit_transformed(&dst, &spr, 0, 0, 0u, NAN, false, 0u);
    CHECK(px_at(&dst, 0, 0) == 0xff000000u);

    sr_canvas_free(&spr);

    CHECK(sr_canvas_init(&spr, 3, 2));
    for (int i = 0; i < 6; i++)
        sr_px(&spr, i % 3, i / 3, (uint32_t)(i + 1) * 0x10101u);

    /* Top-left clip preserves transformed coordinates (AD/BE/CF). */
    sr_clear(&dst, 0x000000u);
    sr_blit_transformed(&dst, &spr, -1, -1, SR_TRANSFORM_FLIP_DIAGONAL,
                        1.0f, false, 0u);
    CHECK(px_at(&dst, 0, 0) == 0xff050505u);
    CHECK(px_at(&dst, 0, 1) == 0xff060606u);
    CHECK(px_at(&dst, 1, 0) == 0xff000000u);

    /* Bottom-right clip covers only the visible A/B segment. */
    sr_clear(&dst, 0x000000u);
    sr_blit_transformed(&dst, &spr, 3, 2, SR_TRANSFORM_FLIP_DIAGONAL,
                        1.0f, false, 0u);
    CHECK(px_at(&dst, 3, 2) == 0xff010101u);
    CHECK(px_at(&dst, 3, 3) == 0xff020202u);
    CHECK(px_at(&dst, 2, 3) == 0xff000000u);

    /* Entirely off-canvas and unknown flag combinations are deterministic
     * no-ops, including coordinates at both signed-int extremes. */
    sr_clear(&dst, 0x123456u);
    sr_blit_transformed(&dst, &spr, INT_MAX, 0,
                        SR_TRANSFORM_FLIP_DIAGONAL, 1.0f, false, 0u);
    sr_blit_transformed(&dst, &spr, INT_MIN, 0,
                        SR_TRANSFORM_FLIP_DIAGONAL, 1.0f, false, 0u);
    sr_blit_transformed(&dst, &spr, 0, INT_MAX,
                        SR_TRANSFORM_FLIP_DIAGONAL, 1.0f, false, 0u);
    sr_blit_transformed(&dst, &spr, 0, INT_MIN,
                        SR_TRANSFORM_FLIP_DIAGONAL, 1.0f, false, 0u);
    sr_blit_transformed(&dst, &spr, 0, 0, UINT8_C(8), 1.0f, false, 0u);
    for (int i = 0; i < 16; i++)
        CHECK(dst.px[i] == 0xff123456u);

    sr_canvas_free(&spr);
    sr_canvas_free(&dst);
    return true;
}

static bool
test_letterbox_scaler_geometry(void)
{
    sr_canvas dst, src;

    /* 40x30 source with quadrant colors into a 100x50 destination:
     * height limits, so the fit is 66x50 centered at x = 17..82 with
     * black pillarbox bars */
    CHECK(sr_canvas_init(&src, 40, 30));
    for (int y = 0; y < 30; y++)
        for (int x = 0; x < 40; x++) {
            uint32_t col = x < 20
                ? (y < 15 ? 0xff0000u : 0x0000ffu)
                : (y < 15 ? 0x00ff00u : 0xffff00u);
            sr_px(&src, x, y, col);
        }

    CHECK(sr_canvas_init(&dst, 100, 50));
    sr_scale_canvas(&dst, &src);

    CHECK(px_at(&dst, 16, 25) == 0xff000000u);  /* left bar */
    CHECK(px_at(&dst, 83, 25) == 0xff000000u);  /* right bar */
    CHECK(px_at(&dst, 0, 0) == 0xff000000u);
    CHECK(px_at(&dst, 99, 49) == 0xff000000u);

    CHECK(px_at(&dst, 17, 0) == 0xffff0000u);   /* top-left quadrant */
    CHECK(px_at(&dst, 82, 0) == 0xff00ff00u);   /* top-right */
    CHECK(px_at(&dst, 17, 49) == 0xff0000ffu);  /* bottom-left */
    CHECK(px_at(&dst, 82, 49) == 0xffffff00u);  /* bottom-right */

    /* quadrant boundary stays centered: source x 20 of 40 maps to
     * destination x 17 + 33 */
    CHECK(px_at(&dst, 49, 0) == 0xffff0000u);
    CHECK(px_at(&dst, 50, 0) == 0xff00ff00u);
    CHECK(px_at(&dst, 17 + 32, 0) == 0xffff0000u);
    CHECK(px_at(&dst, 17 + 33, 0) == 0xff00ff00u);
    sr_canvas_free(&src);
    sr_canvas_free(&dst);

    /* width-limited case gets top/bottom bars instead */
    CHECK(sr_canvas_init(&src, 20, 20));
    sr_clear(&src, 0xffffffu);
    CHECK(sr_canvas_init(&dst, 40, 60));
    sr_scale_canvas(&dst, &src);
    CHECK(px_at(&dst, 20, 9) == 0xff000000u);   /* top bar: fit 40x40 at y 10 */
    CHECK(px_at(&dst, 20, 10) == 0xffffffffu);
    CHECK(px_at(&dst, 20, 49) == 0xffffffffu);
    CHECK(px_at(&dst, 20, 50) == 0xff000000u);
    sr_canvas_free(&src);
    sr_canvas_free(&dst);
    return true;
}

static bool
test_selectable_faces(void)
{
    sr_canvas c;
    int fixed_ink = 0, compact_ink = 0, legacy_ink = 0, i;
    unsigned ch;

    /* Both faces advance 8px, so a string occupies the same width in
     * either; only the glyphs and the cell height differ. */
    CHECK(sr_font_advance(SR_FONT_FIXED_8X16) == 8);
    CHECK(sr_font_advance(SR_FONT_COMPACT_7X14) == 8);
    CHECK(sr_font_height(SR_FONT_FIXED_8X16) == 16);
    CHECK(sr_font_height(SR_FONT_COMPACT_7X14) == 14);
    CHECK(sr_text_width_in(SR_FONT_COMPACT_7X14, "Desk", 1) == 32);
    CHECK(sr_text_width_in(SR_FONT_COMPACT_7X14, "Desk", 3) == 96);
    CHECK(sr_text_width_in(SR_FONT_COMPACT_7X14, "", 1) == 0);

    /* An unknown face measures and draws nothing rather than quietly
     * falling back to a face the caller did not ask for. */
    CHECK(sr_font_advance(SR_FONT_COUNT) == 0);
    CHECK(sr_font_height(SR_FONT_COUNT) == 0);
    CHECK(sr_font_glyph_in(SR_FONT_COUNT, 'A') == NULL);
    CHECK(sr_text_width_in(SR_FONT_COUNT, "Desk", 1) == 0);

    /* Every printable character carries ink, and the space carries none:
     * a blank glyph would satisfy a lookup and still leave a hole. */
    for (ch = 33u; ch <= 126u; ++ch) {
        const uint8_t *glyph =
            sr_font_glyph_in(SR_FONT_COMPACT_7X14, (unsigned char)ch);
        int ink = 0, row;
        CHECK(glyph != NULL);
        for (row = 0; row < 14; ++row) ink |= glyph[row];
        CHECK(ink != 0);
    }
    {
        const uint8_t *glyph = sr_font_glyph_in(SR_FONT_COMPACT_7X14, ' ');
        int row;
        for (row = 0; row < 14; ++row) CHECK(glyph[row] == 0);
    }
    /* Seven wide inside an eight cell: the rightmost column stays clear,
     * which is what keeps adjacent characters from touching. */
    for (ch = 32u; ch <= 126u; ++ch) {
        const uint8_t *glyph =
            sr_font_glyph_in(SR_FONT_COMPACT_7X14, (unsigned char)ch);
        int row;
        for (row = 0; row < 14; ++row) CHECK((glyph[row] & 0x01u) == 0u);
    }

    /* Selecting a face changes what is drawn. */
    CHECK(sr_canvas_init(&c, 64, 32));
    sr_clear(&c, 0x000000u);
    sr_text_in(SR_FONT_FIXED_8X16, &c, 0.0f, 0.0f, "A", 0xffffffu, 1.0f, 1);
    for (i = 0; i < 64 * 32; ++i) if (c.px[i] & 0xffffffu) ++fixed_ink;
    sr_clear(&c, 0x000000u);
    sr_text_in(SR_FONT_COMPACT_7X14, &c, 0.0f, 0.0f, "A", 0xffffffu, 1.0f, 1);
    for (i = 0; i < 64 * 32; ++i) if (c.px[i] & 0xffffffu) ++compact_ink;
    CHECK(fixed_ink > 0);
    CHECK(compact_ink > 0);
    CHECK(fixed_ink != compact_ink);

    /* The original entry points keep drawing the default face. */
    sr_clear(&c, 0x000000u);
    sr_text(&c, 0.0f, 0.0f, "A", 0xffffffu, 1.0f, 1);
    for (i = 0; i < 64 * 32; ++i) if (c.px[i] & 0xffffffu) ++legacy_ink;
    CHECK(legacy_ink == fixed_ink);
    CHECK(sr_text_width("Desk", 1) ==
          sr_text_width_in(SR_FONT_FIXED_8X16, "Desk", 1));

    sr_canvas_free(&c);
    return true;
}

static bool
test_ppm_round_trip(void)
{
    const char *path = "build/test-roundtrip.ppm";
    const char *invalid_path = "build/test-invalid.ppm";
    sr_canvas c;
    FILE *file;
    char magic[3] = {0};
    int w = 0, h = 0, maxval = 0;
    unsigned char bytes[3 * 2 * 3];

    CHECK(sr_canvas_init(&c, 3, 2));
    sr_px(&c, 0, 0, 0x102030u);
    sr_px(&c, 1, 0, 0xff0000u);
    sr_px(&c, 2, 0, 0x00ff00u);
    sr_px(&c, 0, 1, 0x0000ffu);
    sr_px(&c, 1, 1, 0xffffffu);
    sr_px(&c, 2, 1, 0x000000u);
    CHECK(sr_write_ppm(&c, path));
    sr_canvas_free(&c);

    file = fopen(path, "rb");
    CHECK(file != NULL);
    CHECK(fscanf(file, "%2s %d %d %d", magic, &w, &h, &maxval) == 4);
    CHECK(strcmp(magic, "P6") == 0);
    CHECK(w == 3 && h == 2 && maxval == 255);
    CHECK(fgetc(file) == '\n');  /* single whitespace before the raster */
    CHECK(fread(bytes, 1, sizeof bytes, file) == sizeof bytes);
    CHECK(fgetc(file) == EOF);   /* no trailing bytes */
    CHECK(fclose(file) == 0);

    CHECK(bytes[0] == 0x10 && bytes[1] == 0x20 && bytes[2] == 0x30);
    CHECK(bytes[3] == 0xff && bytes[4] == 0x00 && bytes[5] == 0x00);
    CHECK(bytes[6] == 0x00 && bytes[7] == 0xff && bytes[8] == 0x00);
    CHECK(bytes[9] == 0x00 && bytes[10] == 0x00 && bytes[11] == 0xff);
    CHECK(bytes[12] == 0xff && bytes[13] == 0xff && bytes[14] == 0xff);
    CHECK(bytes[15] == 0x00 && bytes[16] == 0x00 && bytes[17] == 0x00);

    errno = 0;
    CHECK(!sr_write_ppm(NULL, path));
    CHECK(errno == EINVAL);

    sr_canvas loaded;
    CHECK(sr_load_ppm(&loaded, path));
    CHECK(loaded.w == 3 && loaded.h == 2 && loaded.owns_px);
    CHECK(px_at(&loaded, 0, 0) == 0xff102030u);
    CHECK(px_at(&loaded, 2, 1) == 0xff000000u);
    sr_canvas_free(&loaded);
    file = fopen(invalid_path, "wb");
    CHECK(file != NULL);
    CHECK(fputs("P6\n999999999999999999999 1\n255\n", file) >= 0);
    CHECK(fclose(file) == 0);
    errno = 0;
    CHECK(!sr_load_ppm(&loaded, invalid_path));
    CHECK(errno == EINVAL);
    CHECK(remove(invalid_path) == 0);
    errno = 0;
    CHECK(!sr_load_ppm(&loaded, "build/does-not-exist.ppm"));
    CHECK(errno == ENOENT);
    CHECK(loaded.px == NULL && loaded.w == 0 && loaded.h == 0);
    CHECK(loaded.clip_x0 == 0 && loaded.clip_y0 == 0 &&
          loaded.clip_x1 == 0 && loaded.clip_y1 == 0);
    return true;
}

/* ---- Byte-identity references -------------------------------------- */

/* The primitives below promise byte-for-byte output, so any span skipping
 * or fast path they grow must be provably invisible.  Each reference here
 * walks every canvas pixel and applies the exact per-pixel coverage and
 * blend expressions the primitive documents, through the public
 * sr_blend(); drawing onto identical randomized destinations and
 * comparing whole buffers pins the contract for opaque and translucent
 * alpha alike. */

static uint32_t test_rng_state;

static uint32_t
test_rng_next(void)
{
    test_rng_state = test_rng_state * 1664525u + 1013904223u;
    return test_rng_state;
}

static void
randomize_canvas(sr_canvas *c, uint32_t seed)
{
    const size_t count = (size_t)c->w * (size_t)c->h;
    size_t i;

    test_rng_state = seed;
    for (i = 0u; i < count; i++)
        c->px[i] = test_rng_next();
}

static bool
canvases_identical(const sr_canvas *a, const sr_canvas *b)
{
    return a->w == b->w && a->h == b->h &&
           memcmp(a->px, b->px,
                  (size_t)a->w * (size_t)a->h * sizeof(uint32_t)) == 0;
}

static float
ref_clampf(float v, float lo, float hi)
{
    if (!(v >= lo)) return lo;
    return v > hi ? hi : v;
}

static void
ref_line(sr_canvas *c, float x0, float y0, float x1, float y1,
         float width, uint32_t rgb, float alpha, int dash_on, int dash_off)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    float hw = width * 0.5f;
    float len;
    int64_t period = (int64_t)dash_on + (int64_t)dash_off;
    int x, y;

    if (hw < 0.5f) hw = 0.5f;
    if (len2 < 0.25f) {
        sr_fill_circle(c, x0, y0, hw, rgb, alpha);
        return;
    }
    len = sqrtf(len2);
    for (y = 0; y < c->h; y++)
        for (x = 0; x < c->w; x++) {
            float px = (float)x + 0.5f - x0;
            float py = (float)y + 0.5f - y0;
            float t = ref_clampf((px * dx + py * dy) / len2, 0.0f, 1.0f);
            float qx = px - t * dx;
            float qy = py - t * dy;
            float cov = hw + 0.5f - sqrtf(qx * qx + qy * qy);
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            if (dash_on >= 0 && dash_off >= 0 && period > 0 &&
                fmodf(t * len, (float)period) >= (float)dash_on) continue;
            sr_blend(c, x, y, rgb, alpha * cov);
        }
}

static void
ref_fill_rect(sr_canvas *c, float x, float y, float w, float h,
              uint32_t rgb, float alpha)
{
    int px, py;

    for (py = 0; py < c->h; py++) {
        float cy = fminf((float)(py + 1), y + h) - fmaxf((float)py, y);
        if (cy <= 0.0f) continue;
        if (cy > 1.0f) cy = 1.0f;
        for (px = 0; px < c->w; px++) {
            float cx = fminf((float)(px + 1), x + w) - fmaxf((float)px, x);
            if (cx <= 0.0f) continue;
            if (cx > 1.0f) cx = 1.0f;
            sr_blend(c, px, py, rgb, alpha * cx * cy);
        }
    }
}

static void
ref_ring(sr_canvas *c, float cx, float cy, float r, float width,
         uint32_t rgb, float alpha)
{
    float hw = width * 0.5f;
    int x, y;

    for (y = 0; y < c->h; y++)
        for (x = 0; x < c->w; x++) {
            float dx = (float)x + 0.5f - cx;
            float dy = (float)y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = hw + 0.5f - fabsf(d - r);
            if (cov <= 0.0f) continue;
            sr_blend(c, x, y, rgb,
                     alpha * (cov > 1.0f ? 1.0f : cov));
        }
}

static bool
test_line_matches_blend_reference(void)
{
    static const struct {
        float x0, y0, x1, y1, width, alpha;
        int dash_on, dash_off;
    } cases[] = {
        {0.0f, 0.0f, 63.0f, 63.0f, 1.0f, 1.0f, 0, 0},
        {0.0f, 63.0f, 63.0f, 0.0f, 2.0f, 0.375f, 0, 0},
        {5.2f, 3.7f, 58.9f, 47.1f, 4.5f, 1.0f, 0, 0},
        {-20.0f, -10.0f, 90.0f, 80.0f, 3.0f, 0.6f, 5, 3},
        {31.5f, -8.0f, 33.5f, 70.0f, 1.0f, 1.0f, 0, 0},
        {-8.0f, 30.25f, 70.0f, 33.75f, 2.5f, 0.5f, 7, 2},
        {10.0f, 50.0f, 55.0f, 12.0f, 8.0f, 0.25f, 0, 0},
    };
    size_t i;
    sr_canvas got, want;

    for (i = 0u; i < sizeof cases / sizeof cases[0]; i++) {
        const uint32_t seed = 0x51a7e000u + (uint32_t)i;

        CHECK(sr_canvas_init(&got, 64, 64));
        CHECK(sr_canvas_init(&want, 64, 64));
        randomize_canvas(&got, seed);
        randomize_canvas(&want, seed);
        sr_line(&got, cases[i].x0, cases[i].y0, cases[i].x1, cases[i].y1,
                cases[i].width, 0x37c2a1u, cases[i].alpha,
                cases[i].dash_on, cases[i].dash_off);
        ref_line(&want, cases[i].x0, cases[i].y0, cases[i].x1, cases[i].y1,
                 cases[i].width, 0x37c2a1u, cases[i].alpha,
                 cases[i].dash_on, cases[i].dash_off);
        CHECK(canvases_identical(&got, &want));
        sr_canvas_free(&got);
        sr_canvas_free(&want);
    }

    /* the clip rectangle must bound the line the same way */
    CHECK(sr_canvas_init(&got, 64, 64));
    CHECK(sr_canvas_init(&want, 64, 64));
    randomize_canvas(&got, 0x11ce5eedu);
    randomize_canvas(&want, 0x11ce5eedu);
    sr_canvas_set_clip(&got, 8, 6, 40, 44);
    sr_canvas_set_clip(&want, 8, 6, 40, 44);
    sr_line(&got, -4.0f, 2.0f, 66.0f, 60.0f, 3.5f, 0xf0e0d0u, 0.8f, 0, 0);
    ref_line(&want, -4.0f, 2.0f, 66.0f, 60.0f, 3.5f, 0xf0e0d0u, 0.8f, 0, 0);
    CHECK(canvases_identical(&got, &want));
    sr_canvas_free(&got);
    sr_canvas_free(&want);
    return true;
}

static bool
test_fill_rect_matches_blend_reference(void)
{
    static const struct {
        float x, y, w, h, alpha;
    } cases[] = {
        {3.3f, 4.7f, 25.6f, 17.2f, 1.0f},
        {3.3f, 4.7f, 25.6f, 17.2f, 0.375f},
        {-5.5f, -2.25f, 40.75f, 30.5f, 1.0f},
        {0.0f, 0.0f, 64.0f, 64.0f, 1.0f},
        {0.5f, 0.5f, 63.0f, 63.0f, 0.5f},
        {10.0f, 12.0f, 0.4f, 20.0f, 1.0f},
    };
    size_t i;
    sr_canvas got, want;

    for (i = 0u; i < sizeof cases / sizeof cases[0]; i++) {
        const uint32_t seed = 0x2ec70000u + (uint32_t)i;

        CHECK(sr_canvas_init(&got, 64, 64));
        CHECK(sr_canvas_init(&want, 64, 64));
        randomize_canvas(&got, seed);
        randomize_canvas(&want, seed);
        sr_fill_rect(&got, cases[i].x, cases[i].y, cases[i].w, cases[i].h,
                     0xb45a92u, cases[i].alpha);
        ref_fill_rect(&want, cases[i].x, cases[i].y, cases[i].w, cases[i].h,
                      0xb45a92u, cases[i].alpha);
        CHECK(canvases_identical(&got, &want));
        sr_canvas_free(&got);
        sr_canvas_free(&want);
    }

    CHECK(sr_canvas_init(&got, 64, 64));
    CHECK(sr_canvas_init(&want, 64, 64));
    randomize_canvas(&got, 0x2ec7c11bu);
    randomize_canvas(&want, 0x2ec7c11bu);
    sr_canvas_set_clip(&got, 5, 9, 30, 28);
    sr_canvas_set_clip(&want, 5, 9, 30, 28);
    sr_fill_rect(&got, 2.5f, 3.75f, 50.0f, 40.0f, 0x87cefau, 1.0f);
    ref_fill_rect(&want, 2.5f, 3.75f, 50.0f, 40.0f, 0x87cefau, 1.0f);
    CHECK(canvases_identical(&got, &want));
    sr_canvas_free(&got);
    sr_canvas_free(&want);
    return true;
}

static bool
test_ring_matches_blend_reference(void)
{
    static const struct {
        float cx, cy, r, width, alpha;
    } cases[] = {
        {32.0f, 32.0f, 20.0f, 3.0f, 1.0f},
        {32.0f, 32.0f, 28.0f, 1.5f, 0.375f},
        {32.0f, 32.0f, 5.0f, 2.0f, 1.0f},
        {10.3f, 52.8f, 25.0f, 4.5f, 0.5f},
        {32.5f, 31.5f, 14.25f, 0.5f, 1.0f},
        {31.0f, 33.0f, 3.0f, 9.0f, 0.75f},
    };
    size_t i;
    sr_canvas got, want;

    for (i = 0u; i < sizeof cases / sizeof cases[0]; i++) {
        const uint32_t seed = 0x21360000u + (uint32_t)i;

        CHECK(sr_canvas_init(&got, 64, 64));
        CHECK(sr_canvas_init(&want, 64, 64));
        randomize_canvas(&got, seed);
        randomize_canvas(&want, seed);
        sr_ring(&got, cases[i].cx, cases[i].cy, cases[i].r, cases[i].width,
                0x5f9ea0u, cases[i].alpha);
        ref_ring(&want, cases[i].cx, cases[i].cy, cases[i].r, cases[i].width,
                 0x5f9ea0u, cases[i].alpha);
        CHECK(canvases_identical(&got, &want));
        sr_canvas_free(&got);
        sr_canvas_free(&want);
    }

    CHECK(sr_canvas_init(&got, 64, 64));
    CHECK(sr_canvas_init(&want, 64, 64));
    randomize_canvas(&got, 0x21365ee0u);
    randomize_canvas(&want, 0x21365ee0u);
    sr_canvas_set_clip(&got, 12, 10, 36, 40);
    sr_canvas_set_clip(&want, 12, 10, 36, 40);
    sr_ring(&got, 30.0f, 34.0f, 22.0f, 2.5f, 0xdeb887u, 0.9f);
    ref_ring(&want, 30.0f, 34.0f, 22.0f, 2.5f, 0xdeb887u, 0.9f);
    CHECK(canvases_identical(&got, &want));
    sr_canvas_free(&got);
    sr_canvas_free(&want);
    return true;
}

/* The hard-edged fills apply one uniform alpha to every covered pixel, so
 * the covered set can be captured once (an opaque draw over transparency
 * lands exactly on 0xffffffff) and replayed through sr_blend() or an
 * opaque store to predict both translucent and opaque results exactly. */
static void
shape_concave_polygon(sr_canvas *c, uint32_t rgb, float alpha)
{
    static const float xs[6] = {6.0f, 52.0f, 52.0f, 26.0f, 26.0f, 6.0f};
    static const float ys[6] = {8.0f, 8.0f, 26.0f, 26.0f, 56.0f, 56.0f};

    sr_fill_polygon(c, xs, ys, 6u, rgb, alpha);
}

static void
shape_triangle(sr_canvas *c, uint32_t rgb, float alpha)
{
    sr_fill_triangle(c, 3.5f, 4.0f, 60.0f, 12.0f, 18.0f, 58.0f, rgb, alpha);
}

static void
shape_convex(sr_canvas *c, uint32_t rgb, float alpha)
{
    static const float xs[5] = {12.0f, 44.0f, 58.0f, 30.0f, 6.0f};
    static const float ys[5] = {4.0f, 6.0f, 40.0f, 60.0f, 30.0f};

    sr_fill_convex(c, xs, ys, 5u, rgb, alpha);
}

static bool
shape_matches_blend_reference(void (*draw)(sr_canvas *, uint32_t, float),
                              uint32_t seed)
{
    sr_canvas mask, got, want;
    int x, y;
    bool covered_some = false;

    CHECK(sr_canvas_init(&mask, 64, 64));
    draw(&mask, 0xffffffu, 1.0f);
    CHECK(sr_canvas_init(&got, 64, 64));
    CHECK(sr_canvas_init(&want, 64, 64));

    randomize_canvas(&got, seed);
    randomize_canvas(&want, seed);
    draw(&got, 0x123456u, 1.0f);
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            const uint32_t m = px_at(&mask, x, y);

            CHECK(m == 0u || m == 0xffffffffu);
            if (m == 0u) continue;
            covered_some = true;
            want.px[(size_t)y * 64u + (size_t)x] = 0xff123456u;
        }
    CHECK(covered_some);
    CHECK(canvases_identical(&got, &want));

    randomize_canvas(&got, seed ^ 0x9e3779b9u);
    randomize_canvas(&want, seed ^ 0x9e3779b9u);
    draw(&got, 0x89abcdu, 0.375f);
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++)
            if (px_at(&mask, x, y) != 0u)
                sr_blend(&want, x, y, 0x89abcdu, 0.375f);
    CHECK(canvases_identical(&got, &want));

    sr_canvas_free(&mask);
    sr_canvas_free(&got);
    sr_canvas_free(&want);
    return true;
}

static bool
test_uniform_fills_match_blend_reference(void)
{
    CHECK(shape_matches_blend_reference(shape_concave_polygon, 0x0badf00du));
    CHECK(shape_matches_blend_reference(shape_triangle, 0x12345678u));
    CHECK(shape_matches_blend_reference(shape_convex, 0x5eedbea7u));
    return true;
}

static bool
test_text_wrappers_match_selected_face(void)
{
    sr_canvas a, b;

    CHECK(sr_canvas_init(&a, 96, 40));
    CHECK(sr_canvas_init(&b, 96, 40));
    randomize_canvas(&a, 0x7e577e57u);
    randomize_canvas(&b, 0x7e577e57u);
    sr_text(&a, -3.2f, 5.9f, "Mixed 123 !?", 0x40c080u, 0.8f, 2);
    sr_text_in(SR_FONT_FIXED_8X16, &b, -3.2f, 5.9f, "Mixed 123 !?",
               0x40c080u, 0.8f, 2);
    CHECK(canvases_identical(&a, &b));

    randomize_canvas(&a, 0x00c0ffeeu);
    randomize_canvas(&b, 0x00c0ffeeu);
    sr_canvas_set_clip(&a, 10, 4, 60, 30);
    sr_canvas_set_clip(&b, 10, 4, 60, 30);
    sr_text_center(&a, 48.0f, 7.5f, "Centered text", 0xffffffu, 0.6f, 1);
    sr_text_center_in(SR_FONT_FIXED_8X16, &b, 48.0f, 7.5f, "Centered text",
                      0xffffffu, 0.6f, 1);
    CHECK(canvases_identical(&a, &b));
    sr_canvas_free(&a);
    sr_canvas_free(&b);
    return true;
}

static bool
test_scale_canvas_clear_and_alias_cases(void)
{
    sr_canvas dst, src, before;
    size_t i;

    /* an unusable source still clears the whole destination */
    CHECK(sr_canvas_init(&dst, 10, 10));
    randomize_canvas(&dst, 0x5ca1ab1eu);
    sr_scale_canvas(&dst, NULL);
    for (i = 0u; i < 100u; i++)
        CHECK(dst.px[i] == 0xff000000u);

    /* an extreme aspect ratio whose fit rounds to zero clears too */
    CHECK(sr_canvas_init(&src, 1000, 1));
    sr_clear(&src, 0xffffffu);
    randomize_canvas(&dst, 0x0ddba11u);
    sr_scale_canvas(&dst, &src);
    for (i = 0u; i < 100u; i++)
        CHECK(dst.px[i] == 0xff000000u);
    sr_canvas_free(&src);

    /* scaling a canvas onto itself is the documented no-op */
    CHECK(sr_canvas_init(&before, 10, 10));
    randomize_canvas(&dst, 0xfeedc0deu);
    randomize_canvas(&before, 0xfeedc0deu);
    sr_scale_canvas(&dst, &dst);
    CHECK(canvases_identical(&dst, &before));
    sr_canvas_free(&before);
    sr_canvas_free(&dst);
    return true;
}

typedef bool (*test_function)(void);

typedef struct test_case {
    const char *name;
    test_function function;
} test_case;

int
main(void)
{
    static const test_case tests[] = {
        {"canvas lifecycle and overflow guard", test_canvas_lifecycle_and_overflow},
        {"wrap does not free caller memory", test_wrap_does_not_free_caller_memory},
        {"clip and RGBA pack", test_clip_and_rgba_pack},
        {"nonfinite, overflow, and degenerate inputs",
         test_nonfinite_overflow_and_degenerate_inputs},
        {"clipped pixel stores", test_clipped_pixel_stores},
        {"blend math", test_blend_math},
        {"color helpers", test_color_helpers},
        {"fill_rect edge coverage", test_fill_rect_edge_coverage},
        {"fill_circle rim coverage", test_fill_circle_rim_coverage},
        {"ring coverage", test_ring_coverage},
        {"line width, dash, and coverage", test_line_width_dash_and_coverage},
        {"fill_triangle", test_fill_triangle},
        {"ellipse and convex fill", test_ellipse_and_convex_fill},
        {"fill_polygon concave", test_fill_polygon_concave},
        {"fill_polygon matches convex", test_fill_polygon_matches_convex},
        {"fill_polygon tiles without seams",
         test_fill_polygon_tiles_without_seams},
        {"fill_polygon winding and self-intersection",
         test_fill_polygon_winding_and_selfintersection},
        {"fill_polygon clipping and rejects",
         test_fill_polygon_clipping_and_rejects},
        {"fill_polygon alpha", test_fill_polygon_alpha},
        {"text metrics and glyph bits", test_text_metrics_and_glyph_bits},
        {"text outline and shadow", test_text_outline_and_shadow},
        {"blit clipping at all edges", test_blit_clipping_all_edges},
        {"overlapping blit", test_overlapping_blit},
        {"blit alpha and tint", test_blit_alpha_and_tint},
        {"scaled blit dimensions", test_blit_scaled_dimensions},
        {"transformed blit combinations", test_blit_transformed_all_combinations},
        {"transformed blit composition and clipping",
         test_blit_transformed_composition_and_clipping},
        {"letterbox scaler geometry", test_letterbox_scaler_geometry},
        {"PPM load/write round-trip", test_ppm_round_trip},
        {"selectable faces", test_selectable_faces},
        {"line matches blend reference", test_line_matches_blend_reference},
        {"fill_rect matches blend reference",
         test_fill_rect_matches_blend_reference},
        {"ring matches blend reference", test_ring_matches_blend_reference},
        {"uniform fills match blend reference",
         test_uniform_fills_match_blend_reference},
        {"text wrappers match selected face",
         test_text_wrappers_match_selected_face},
        {"scale_canvas clear and alias cases",
         test_scale_canvas_clear_and_alias_cases}
    };
    size_t passed = 0u;
    size_t index;

    for (index = 0u; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        const bool ok = tests[index].function();

        (void)printf("%s %s\n", ok ? "ok" : "not ok", tests[index].name);
        if (!ok) {
            return 1;
        }
        ++passed;
    }
    (void)printf("%zu tests passed\n", passed);
    return 0;
}
