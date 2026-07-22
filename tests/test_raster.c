#include "soft_raster.h"

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
    return true;
}

static bool
test_clip_and_rgba_pack(void)
{
    sr_canvas c;
    uint8_t rgba[4u * 4u * 4u] = {0};

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
test_ppm_round_trip(void)
{
    const char *path = "build/test-roundtrip.ppm";
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

    CHECK(!sr_write_ppm(NULL, path));

    sr_canvas loaded;
    CHECK(sr_load_ppm(&loaded, path));
    CHECK(loaded.w == 3 && loaded.h == 2 && loaded.owns_px);
    CHECK(px_at(&loaded, 0, 0) == 0xff102030u);
    CHECK(px_at(&loaded, 2, 1) == 0xff000000u);
    sr_canvas_free(&loaded);
    CHECK(!sr_load_ppm(&loaded, "build/does-not-exist.ppm"));
    CHECK(loaded.px == NULL && loaded.w == 0 && loaded.h == 0);
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
        {"clipped pixel stores", test_clipped_pixel_stores},
        {"blend math", test_blend_math},
        {"color helpers", test_color_helpers},
        {"fill_rect edge coverage", test_fill_rect_edge_coverage},
        {"fill_circle rim coverage", test_fill_circle_rim_coverage},
        {"ring coverage", test_ring_coverage},
        {"line width, dash, and coverage", test_line_width_dash_and_coverage},
        {"fill_triangle", test_fill_triangle},
        {"ellipse and convex fill", test_ellipse_and_convex_fill},
        {"text metrics and glyph bits", test_text_metrics_and_glyph_bits},
        {"text outline and shadow", test_text_outline_and_shadow},
        {"blit clipping at all edges", test_blit_clipping_all_edges},
        {"blit alpha and tint", test_blit_alpha_and_tint},
        {"scaled blit dimensions", test_blit_scaled_dimensions},
        {"transformed blit combinations", test_blit_transformed_all_combinations},
        {"transformed blit composition and clipping",
         test_blit_transformed_composition_and_clipping},
        {"letterbox scaler geometry", test_letterbox_scaler_geometry},
        {"PPM load/write round-trip", test_ppm_round_trip}
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
