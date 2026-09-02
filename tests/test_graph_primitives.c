#include "soft_raster.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint32_t at(const sr_canvas *c, int x, int y) { return c->px[(size_t)y * (size_t)c->w + (size_t)x]; }
static int blue(const sr_canvas *c, int x, int y) { return (int)(at(c, x, y) & 255u); }

int main(void)
{
    /* 1. Two-point round-cap polyline equals sr_line, pixel for pixel. */
    {
        sr_canvas a, b;
        sr_canvas_init(&a, 80, 40); sr_canvas_init(&b, 80, 40);
        sr_clear(&a, 0x000000); sr_clear(&b, 0x000000);
        sr_line(&a, 7.3f, 6.1f, 70.5f, 33.9f, 4.0f, 0xFFFFFF, 0.75f, 0, 0);
        float xs[2] = {7.3f, 70.5f}, ys[2] = {6.1f, 33.9f};
        sr_polyline(&b, xs, ys, 2, 4.0f, 0xFFFFFF, 0.75f, 0, 0, 0.0f, SR_CAP_ROUND);
        CHECK(memcmp(a.px, b.px, (size_t)80 * 40 * sizeof(uint32_t)) == 0,
              "2-point round polyline differs from sr_line");
        sr_canvas_free(&a); sr_canvas_free(&b);
    }

    /* 2. No overdraw at a joint: the joint blends exactly once. */
    {
        sr_canvas c; sr_canvas_init(&c, 120, 60); sr_clear(&c, 0x000000);
        float xs[3] = {10, 60, 110}, ys[3] = {30, 30, 30};
        sr_polyline(&c, xs, ys, 3, 6.0f, 0xFFFFFF, 0.5f, 0, 0, 0.0f, SR_CAP_ROUND);
        CHECK(blue(&c, 60, 30) == blue(&c, 30, 30),
              "joint %d != segment %d", blue(&c, 60, 30), blue(&c, 30, 30));
        sr_canvas_free(&c);
    }

    /* 3. Corner joint likewise. */
    {
        sr_canvas c; sr_canvas_init(&c, 120, 120); sr_clear(&c, 0x000000);
        float xs[3] = {10, 60, 60}, ys[3] = {30, 30, 100};
        sr_polyline(&c, xs, ys, 3, 6.0f, 0xFFFFFF, 0.5f, 0, 0, 0.0f, SR_CAP_ROUND);
        CHECK(blue(&c, 60, 30) == blue(&c, 30, 30),
              "corner joint %d != arm %d", blue(&c, 60, 30), blue(&c, 30, 30));
        sr_canvas_free(&c);
    }

    /* 4. Dash phase runs along the whole path, not per segment. */
    {
        sr_canvas c; sr_canvas_init(&c, 200, 20); sr_clear(&c, 0x000000);
        float xs[3] = {0, 100, 200}, ys[3] = {10, 10, 10};
        sr_polyline(&c, xs, ys, 3, 2.0f, 0xFFFFFF, 1.0f, 8, 8, 0.0f, SR_CAP_BUTT);
        int run = 0, worst = 0;
        for (int x = 0; x < 200; x++) {
            if (blue(&c, x, 10) > 64) { run++; if (run > worst) worst = run; }
            else run = 0;
        }
        CHECK(worst <= 8, "dash run of %d exceeds dash_on 8 (phase restarted)", worst);
        sr_canvas_free(&c);
    }

    /* 5. dash_offset shifts the pattern. */
    {
        sr_canvas c; sr_canvas_init(&c, 64, 20); sr_clear(&c, 0x000000);
        float xs[2] = {0, 64}, ys[2] = {10, 10};
        sr_polyline(&c, xs, ys, 2, 2.0f, 0xFFFFFF, 1.0f, 4, 4, 4.0f, SR_CAP_BUTT);
        CHECK(blue(&c, 1, 10) < 64 && blue(&c, 5, 10) > 64,
              "dash_offset 4 did not invert the first period");
        sr_canvas_free(&c);
    }

    /* 6. Butt cap stops at the end point; round cap runs past it. */
    {
        sr_canvas r, b;
        sr_canvas_init(&r, 40, 20); sr_canvas_init(&b, 40, 20);
        sr_clear(&r, 0x000000); sr_clear(&b, 0x000000);
        float xs[2] = {10, 30}, ys[2] = {10, 10};
        sr_polyline(&r, xs, ys, 2, 6.0f, 0xFFFFFF, 1.0f, 0, 0, 0.0f, SR_CAP_ROUND);
        sr_polyline(&b, xs, ys, 2, 6.0f, 0xFFFFFF, 1.0f, 0, 0, 0.0f, SR_CAP_BUTT);
        CHECK(blue(&r, 32, 10) > 128, "round cap missing past the end point");
        CHECK(blue(&b, 32, 10) == 0, "butt cap drew past the end point");
        CHECK(blue(&b, 29, 10) > 128, "butt cap ate the last pixel");
        sr_canvas_free(&r); sr_canvas_free(&b);
    }

    /* 7. AA polygon fill produces fractional coverage where the hard one
     *    produces only 0 and 255, and agrees on the interior. */
    {
        sr_canvas h, a;
        sr_canvas_init(&h, 64, 64); sr_canvas_init(&a, 64, 64);
        sr_clear(&h, 0x000000); sr_clear(&a, 0x000000);
        float xs[3] = {8, 56, 8}, ys[3] = {8, 30, 52};
        sr_fill_polygon(&h, xs, ys, 3, 0xFFFFFF, 1.0f);
        sr_fill_polygon_aa(&a, xs, ys, 3, 0xFFFFFF, 1.0f);
        int partial = 0, hard_count = 0;
        double aa_sum = 0.0;
        for (int x = 0; x < 64; x++) {
            int v = blue(&a, x, 14);
            if (v > 0 && v < 255) partial++;
            aa_sum += (double)v / 255.0;
            if (blue(&h, x, 14) == 255) hard_count++;
        }
        CHECK(partial >= 1, "AA fill produced no fractional coverage");
        /* Same shape, so the covered area agrees to within a pixel or two;
         * the difference is exactly the edge the center sample rounds off. */
        CHECK(fabs(aa_sum - (double)hard_count) < 2.0,
              "AA area %.2f differs from hard area %d by more than 2px",
              aa_sum, hard_count);
        /* Deep interior is opaque in both. */
        CHECK(blue(&a, 12, 30) == 255 && blue(&h, 12, 30) == 255,
              "AA fill did not fill the interior");
        sr_canvas_free(&h); sr_canvas_free(&a);
    }

    /* 8. Rounded rectangle: corners cut, edges anti-aliased, center filled. */
    {
        sr_canvas c; sr_canvas_init(&c, 64, 48); sr_clear(&c, 0x000000);
        sr_fill_round_rect(&c, 8, 8, 48, 32, 10, 0xFFFFFF, 1.0f);
        CHECK(blue(&c, 32, 24) == 255, "round rect center not filled");
        CHECK(blue(&c, 9, 9) == 0, "round rect corner not cut");
        /* The corner arc crosses pixels at every angle, so it always carries
         * fractional coverage; an axis-aligned edge only does when it falls
         * between pixel centers, which is checked separately below. */
        int arc_partial = 0;
        for (int y = 8; y < 20; y++)
            for (int x = 8; x < 20; x++) {
                int v = blue(&c, x, y);
                if (v > 0 && v < 255) arc_partial++;
            }
        CHECK(arc_partial >= 8,
              "round rect corner arc not anti-aliased (%d partial pixels)",
              arc_partial);
        sr_canvas_free(&c);

        /* A top edge at y = 8.5 splits the pixel row exactly in half. */
        sr_canvas_init(&c, 64, 48);
        sr_clear(&c, 0x000000);
        sr_fill_round_rect(&c, 8, 8.5f, 48, 32, 10, 0xFFFFFF, 1.0f);
        CHECK(blue(&c, 32, 8) > 96 && blue(&c, 32, 8) < 160,
              "half-covered top row read %d, expected about 128",
              blue(&c, 32, 8));
        sr_canvas_free(&c);
    }

    /* 9. r <= 0 gives square corners. */
    {
        sr_canvas c; sr_canvas_init(&c, 64, 48); sr_clear(&c, 0x000000);
        sr_fill_round_rect(&c, 8, 8, 48, 32, 0, 0xFFFFFF, 1.0f);
        CHECK(blue(&c, 9, 9) == 255, "r=0 did not give a square corner");
        sr_canvas_free(&c);
    }

    /* 10. Stroked round rect is hollow and centered on the outline. */
    {
        sr_canvas c; sr_canvas_init(&c, 64, 48); sr_clear(&c, 0x000000);
        sr_stroke_round_rect(&c, 8, 8, 48, 32, 8, 3, 0xFFFFFF, 1.0f);
        CHECK(blue(&c, 32, 24) == 0, "stroked round rect filled its interior");
        CHECK(blue(&c, 32, 8) == 255, "stroke not on the top outline");
        sr_canvas_free(&c);
    }

    /* 11. Cubic flattening: sizing pass, adaptivity, and end points. */
    {
        size_t need = sr_flatten_cubic(0, 0, 10, 0, 20, 0, 30, 0, 0.25f, NULL, NULL, 0);
        CHECK(need == 2, "a straight cubic flattened to %zu points", need);

        float xs[1100], ys[1100];
        size_t n = sr_flatten_cubic(0, 0, 0, 100, 100, 100, 100, 0, 0.25f,
                                    xs, ys, 1100);
        CHECK(n > 8 && n <= 1025, "curved cubic flattened to %zu points", n);
        CHECK(xs[0] == 0.0f && ys[0] == 0.0f, "first point is not the start");
        CHECK(xs[n - 1] == 100.0f && ys[n - 1] == 0.0f, "last point is not the end");

        size_t loose = sr_flatten_cubic(0, 0, 0, 100, 100, 100, 100, 0, 8.0f,
                                        xs, ys, 1100);
        CHECK(loose < n, "a looser tolerance did not use fewer points (%zu vs %zu)",
              loose, n);

        /* Truncation reports the full requirement without writing past the end. */
        float small_x[4], small_y[4];
        size_t want = sr_flatten_cubic(0, 0, 0, 100, 100, 100, 100, 0, 0.25f,
                                       small_x, small_y, 4);
        CHECK(want == n, "truncated call reported %zu, full call %zu", want, n);
    }

    /* 12. Degenerate and hostile input is a no-op, not a crash. */
    {
        sr_canvas c; sr_canvas_init(&c, 16, 16); sr_clear(&c, 0x000000);
        float one_x[1] = {5}, one_y[1] = {5};
        sr_polyline(&c, one_x, one_y, 1, 2, 0xFFFFFF, 1.0f, 0, 0, 0, SR_CAP_ROUND);
        sr_polyline(&c, NULL, NULL, 4, 2, 0xFFFFFF, 1.0f, 0, 0, 0, SR_CAP_ROUND);
        float nan_x[2] = {0, NAN}, nan_y[2] = {0, 0};
        sr_polyline(&c, nan_x, nan_y, 2, 2, 0xFFFFFF, 1.0f, 0, 0, 0, SR_CAP_ROUND);
        sr_polyline(&c, one_x, one_y, 2, 2, 0xFFFFFF, 1.0f, 0, 0, 0, (sr_cap)99);
        int touched = 0;
        for (int i = 0; i < 16 * 16; i++) if ((c.px[i] & 255u) != 0) touched++;
        CHECK(touched == 0, "invalid input drew %d pixels", touched);

        /* A zero-length path is a dot, matching sr_line's degenerate case. */
        float dot_x[3] = {8, 8, 8}, dot_y[3] = {8, 8, 8};
        sr_polyline(&c, dot_x, dot_y, 3, 6, 0xFFFFFF, 1.0f, 0, 0, 0, SR_CAP_ROUND);
        CHECK(blue(&c, 8, 8) == 255, "zero-length path drew no dot");
        sr_canvas_free(&c);
    }

    /* 13. Everything stays inside the clip. */
    {
        sr_canvas c; sr_canvas_init(&c, 64, 64); sr_clear(&c, 0x000000);
        sr_canvas_set_clip(&c, 20, 20, 20, 20);
        float xs[3] = {0, 32, 64}, ys[3] = {0, 32, 64};
        sr_polyline(&c, xs, ys, 3, 8, 0xFFFFFF, 1.0f, 0, 0, 0, SR_CAP_ROUND);
        sr_fill_round_rect(&c, 0, 0, 64, 64, 8, 0xFFFFFF, 1.0f);
        float px[3] = {0, 64, 0}, py[3] = {0, 32, 64};
        sr_fill_polygon_aa(&c, px, py, 3, 0xFFFFFF, 1.0f);
        int outside = 0;
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++)
                if ((x < 20 || x >= 40 || y < 20 || y >= 40) && blue(&c, x, y) != 0)
                    outside++;
        CHECK(outside == 0, "%d pixels drawn outside the clip", outside);
        sr_canvas_free(&c);
    }

    if (fails == 0) printf("13 graph-primitive checks passed\n");
    return fails == 0 ? 0 : 1;
}
