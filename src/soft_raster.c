/*
 * soft-raster: anti-aliased software rasterization into 0xAARRGGBB pixels.
 *
 * The blending and coverage math is kept identical to the renderer these
 * routines were extracted from: alpha is quantized to 1/256 steps and each
 * channel moves toward the target color with an arithmetic-shift lerp, so a
 * frame drawn through this library matches the original games byte for byte
 * on an opaque canvas.
 */
#include "soft_raster.h"
#include "font8x16.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static bool canvas_ok(const sr_canvas *c)
{
    return c != NULL && c->px != NULL && c->w > 0 && c->h > 0;
}

static void reset_clip(sr_canvas *c)
{
    c->clip_x0 = 0;
    c->clip_y0 = 0;
    c->clip_x1 = c->w;
    c->clip_y1 = c->h;
}

/* ---------------------------------------------------------------- canvas */

bool sr_canvas_init(sr_canvas *c, int w, int h)
{
    if (c == NULL) return false;
    c->px = NULL;
    c->w = 0;
    c->h = 0;
    reset_clip(c);
    c->owns_px = false;
    if (w <= 0 || h <= 0) return false;
    /* The pixel count must fit an int and the byte count a size_t. */
    if ((uint64_t)w * (uint64_t)h > (uint64_t)INT_MAX) return false;
    if ((size_t)w > SIZE_MAX / sizeof(uint32_t) / (size_t)h) return false;
    uint32_t *px = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if (px == NULL) return false;
    c->px = px;
    c->w = w;
    c->h = h;
    c->owns_px = true;
    reset_clip(c);
    return true;
}

void sr_canvas_wrap(sr_canvas *c, uint32_t *mem, int w, int h)
{
    if (c == NULL) return;
    c->px = mem;
    c->w = mem != NULL && w > 0 ? w : 0;
    c->h = mem != NULL && h > 0 ? h : 0;
    c->owns_px = false;
    reset_clip(c);
}

void sr_canvas_free(sr_canvas *c)
{
    if (c == NULL) return;
    if (c->owns_px) free(c->px);
    c->px = NULL;
    c->w = 0;
    c->h = 0;
    c->owns_px = false;
    reset_clip(c);
}

void sr_canvas_set_clip(sr_canvas *c, int x, int y, int w, int h)
{
    if (c == NULL) return;
    int64_t right = (int64_t)x + w;
    int64_t bottom = (int64_t)y + h;
    c->clip_x0 = x < 0 ? 0 : x > c->w ? c->w : x;
    c->clip_y0 = y < 0 ? 0 : y > c->h ? c->h : y;
    c->clip_x1 = right < 0 ? 0 : right > c->w ? c->w : (int)right;
    c->clip_y1 = bottom < 0 ? 0 : bottom > c->h ? c->h : (int)bottom;
    if (w <= 0 || c->clip_x1 < c->clip_x0) c->clip_x1 = c->clip_x0;
    if (h <= 0 || c->clip_y1 < c->clip_y0) c->clip_y1 = c->clip_y0;
}

void sr_canvas_reset_clip(sr_canvas *c)
{
    if (c != NULL) reset_clip(c);
}

bool sr_pack_rgba(const sr_canvas *c, uint8_t *rgba, size_t byte_count)
{
    if (!canvas_ok(c) || rgba == NULL) return false;
    size_t pixels = (size_t)c->w * (size_t)c->h;
    if (pixels > SIZE_MAX / 4u || byte_count < pixels * 4u) return false;
    for (size_t i = 0u; i < pixels; i++) {
        uint32_t pixel = c->px[i];
        rgba[i * 4u] = (uint8_t)(pixel >> 16);
        rgba[i * 4u + 1u] = (uint8_t)(pixel >> 8);
        rgba[i * 4u + 2u] = (uint8_t)pixel;
        rgba[i * 4u + 3u] = (uint8_t)(pixel >> 24);
    }
    return true;
}

/* ---------------------------------------------------------------- colors */

uint32_t sr_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t sr_mix(uint32_t a, uint32_t b, float t)
{
    t = clampf(t, 0.0f, 1.0f);
    int ar = (int)((a >> 16) & 255u), ag = (int)((a >> 8) & 255u);
    int ab = (int)(a & 255u);
    int br = (int)((b >> 16) & 255u), bg = (int)((b >> 8) & 255u);
    int bb = (int)(b & 255u);
    int r = ar + (int)((float)(br - ar) * t);
    int g = ag + (int)((float)(bg - ag) * t);
    int bl = ab + (int)((float)(bb - ab) * t);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

uint32_t sr_scale_rgb(uint32_t rgb, float k)
{
    k = clampf(k, 0.0f, 2.0f);
    int r = (int)((float)((rgb >> 16) & 255u) * k);
    int g = (int)((float)((rgb >> 8) & 255u) * k);
    int b = (int)((float)(rgb & 255u) * k);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* ---------------------------------------------------------------- pixels */

void sr_clear(sr_canvas *c, uint32_t rgb)
{
    if (!canvas_ok(c)) return;
    uint32_t value = 0xff000000u | (rgb & 0x00ffffffu);
    size_t n = (size_t)c->w * (size_t)c->h;
    for (size_t i = 0; i < n; i++)
        c->px[i] = value;
}

void sr_px(sr_canvas *c, int x, int y, uint32_t rgb)
{
    if (!canvas_ok(c) || x < c->clip_x0 || x >= c->clip_x1 ||
        y < c->clip_y0 || y >= c->clip_y1) return;
    c->px[(size_t)y * (size_t)c->w + (size_t)x] =
        0xff000000u | (rgb & 0x00ffffffu);
}

/* Core blend, shared by every primitive.  ai is coverage in [0,256]; the
 * RGB channels lerp toward the color and the alpha byte lerps toward 255
 * with the same arithmetic-shift fixed-point step the games use. */
static void blend_px(sr_canvas *c, int x, int y, uint32_t rgb, float a)
{
    if (x < c->clip_x0 || x >= c->clip_x1 ||
        y < c->clip_y0 || y >= c->clip_y1) return;
    int ai = (int)(a * 256.0f + 0.5f);
    if (ai <= 0) return;
    if (ai > 256) ai = 256;
    uint32_t *p = &c->px[(size_t)y * (size_t)c->w + (size_t)x];
    uint32_t d = *p;
    int dr = (int)((d >> 16) & 255u);
    int dg = (int)((d >> 8) & 255u);
    int db = (int)(d & 255u);
    int da = (int)(d >> 24);
    int r = (int)((rgb >> 16) & 255u);
    int g = (int)((rgb >> 8) & 255u);
    int b = (int)(rgb & 255u);
    dr += ((r - dr) * ai) >> 8;
    dg += ((g - dg) * ai) >> 8;
    db += ((b - db) * ai) >> 8;
    da += ((255 - da) * ai) >> 8;
    *p = ((uint32_t)da << 24) | ((uint32_t)dr << 16) |
         ((uint32_t)dg << 8) | (uint32_t)db;
}

void sr_blend(sr_canvas *c, int x, int y, uint32_t rgb, float alpha)
{
    if (!canvas_ok(c)) return;
    blend_px(c, x, y, rgb, alpha);
}

/* Clip a float coordinate range to [0, limit) and convert to ints.  The
 * clip only discards pixels blend_px would reject anyway, but it keeps the
 * loop bounds small and the float-to-int casts in range. */
static int clip_lo(float v)
{
    return (int)fmaxf(floorf(v), 0.0f);
}

static int clip_hi(float v, int limit)
{
    return (int)fminf(ceilf(v), (float)limit);
}

/* ------------------------------------------------------------ primitives */

void sr_fill_rect(sr_canvas *c, float x, float y, float w, float h,
                  uint32_t rgb, float alpha)
{
    if (!canvas_ok(c) || w <= 0.0f || h <= 0.0f) return;
    int x0 = clip_lo(x), x1 = clip_hi(x + w, c->w);
    int y0 = clip_lo(y), y1 = clip_hi(y + h, c->h);
    for (int py = y0; py < y1; py++) {
        float cy = fminf((float)(py + 1), y + h) - fmaxf((float)py, y);
        if (cy <= 0.0f) continue;
        if (cy > 1.0f) cy = 1.0f;
        for (int px = x0; px < x1; px++) {
            float cx = fminf((float)(px + 1), x + w) - fmaxf((float)px, x);
            if (cx <= 0.0f) continue;
            if (cx > 1.0f) cx = 1.0f;
            blend_px(c, px, py, rgb, alpha * cx * cy);
        }
    }
}

void sr_stroke_rect(sr_canvas *c, float x, float y, float w, float h,
                    float line, uint32_t rgb, float alpha)
{
    sr_fill_rect(c, x, y, w, line, rgb, alpha);
    sr_fill_rect(c, x, y + h - line, w, line, rgb, alpha);
    sr_fill_rect(c, x, y, line, h, rgb, alpha);
    sr_fill_rect(c, x + w - line, y, line, h, rgb, alpha);
}

void sr_fill_circle(sr_canvas *c, float cx, float cy, float r,
                    uint32_t rgb, float alpha)
{
    if (!canvas_ok(c) || r <= 0.0f) return;
    float r_out = r + 0.5f, r_in = r - 0.5f;
    float r_out2 = r_out * r_out;
    float r_in2 = r_in > 0.0f ? r_in * r_in : 0.0f;
    int y0 = clip_lo(cy - r_out), y1 = clip_hi(cy + r_out, c->h) - 1;
    for (int y = y0; y <= y1; y++) {
        float dy = (float)y + 0.5f - cy;
        float w2 = r_out2 - dy * dy;
        if (w2 <= 0.0f) continue;
        float half = sqrtf(w2);
        int x0 = clip_lo(cx - half), x1 = clip_hi(cx + half, c->w) - 1;
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx;
            float d2 = dx * dx + dy * dy;
            if (d2 >= r_out2) continue;
            if (d2 <= r_in2) {
                blend_px(c, x, y, rgb, alpha);
            } else {
                float cov = r_out - sqrtf(d2);
                blend_px(c, x, y, rgb, alpha * (cov > 1.0f ? 1.0f : cov));
            }
        }
    }
}

void sr_fill_ellipse(sr_canvas *c, float cx, float cy, float rx, float ry,
                     uint32_t rgb, float alpha)
{
    if (!canvas_ok(c) || rx <= 0.0f || ry <= 0.0f) return;
    int x0 = clip_lo(cx - rx - 1.0f);
    int x1 = clip_hi(cx + rx + 1.0f, c->w);
    int y0 = clip_lo(cy - ry - 1.0f);
    int y1 = clip_hi(cy + ry + 1.0f, c->h);
    float edge_scale = fminf(rx, ry);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float dx = ((float)x + 0.5f - cx) / rx;
            float dy = ((float)y + 0.5f - cy) / ry;
            float coverage = (1.0f - sqrtf(dx * dx + dy * dy)) * edge_scale
                           + 0.5f;
            if (coverage <= 0.0f) continue;
            blend_px(c, x, y, rgb, alpha * fminf(coverage, 1.0f));
        }
    }
}

void sr_ring(sr_canvas *c, float cx, float cy, float r, float width,
             uint32_t rgb, float alpha)
{
    if (!canvas_ok(c)) return;
    float hw = width * 0.5f;
    int x0 = clip_lo(cx - r - hw - 1.0f);
    int x1 = clip_hi(cx + r + hw + 1.0f, c->w);
    int y0 = clip_lo(cy - r - hw - 1.0f);
    int y1 = clip_hi(cy + r + hw + 1.0f, c->h);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float dx = (float)x + 0.5f - cx;
            float dy = (float)y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = hw + 0.5f - fabsf(d - r);
            if (cov <= 0.0f) continue;
            blend_px(c, x, y, rgb, alpha * (cov > 1.0f ? 1.0f : cov));
        }
    }
}

void sr_line(sr_canvas *c, float x0, float y0, float x1, float y1,
             float width, uint32_t rgb, float alpha,
             int dash_on, int dash_off)
{
    if (!canvas_ok(c)) return;
    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    float hw = width * 0.5f;
    if (hw < 0.5f) hw = 0.5f;
    if (len2 < 0.25f) {
        sr_fill_circle(c, x0, y0, hw, rgb, alpha);
        return;
    }
    float len = sqrtf(len2);
    int x_min = clip_lo(fminf(x0, x1) - hw - 1.0f);
    int x_max = clip_hi(fmaxf(x0, x1) + hw + 1.0f, c->w);
    int y_min = clip_lo(fminf(y0, y1) - hw - 1.0f);
    int y_max = clip_hi(fmaxf(y0, y1) + hw + 1.0f, c->h);
    int period = dash_on + dash_off;
    for (int y = y_min; y < y_max; y++) {
        for (int x = x_min; x < x_max; x++) {
            float px = (float)x + 0.5f - x0;
            float py = (float)y + 0.5f - y0;
            float t = clampf((px * dx + py * dy) / len2, 0.0f, 1.0f);
            float qx = px - t * dx;
            float qy = py - t * dy;
            float cov = hw + 0.5f - sqrtf(qx * qx + qy * qy);
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            if (period > 0 &&
                fmodf(t * len, (float)period) >= (float)dash_on) continue;
            blend_px(c, x, y, rgb, alpha * cov);
        }
    }
}

static float edge_fn(float ax, float ay, float bx, float by,
                     float px, float py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

void sr_fill_triangle(sr_canvas *c, float x0, float y0, float x1, float y1,
                      float x2, float y2, uint32_t rgb, float alpha)
{
    if (!canvas_ok(c)) return;
    int min_x = clip_lo(fminf(x0, fminf(x1, x2)) - 1.0f);
    int max_x = clip_hi(fmaxf(x0, fmaxf(x1, x2)) + 1.0f, c->w) - 1;
    int min_y = clip_lo(fminf(y0, fminf(y1, y2)) - 1.0f);
    int max_y = clip_hi(fmaxf(y0, fmaxf(y1, y2)) + 1.0f, c->h) - 1;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float e0 = edge_fn(x0, y0, x1, y1, px, py);
            float e1 = edge_fn(x1, y1, x2, y2, px, py);
            float e2 = edge_fn(x2, y2, x0, y0, px, py);
            bool neg = e0 < 0.0f || e1 < 0.0f || e2 < 0.0f;
            bool pos = e0 > 0.0f || e1 > 0.0f || e2 > 0.0f;
            if (!(neg && pos))
                blend_px(c, x, y, rgb, alpha);
        }
    }
}

void sr_fill_convex(sr_canvas *c, const float *xs, const float *ys,
                    size_t count, uint32_t rgb, float alpha)
{
    if (!canvas_ok(c) || xs == NULL || ys == NULL || count < 3u) return;
    float min_x = xs[0], max_x = xs[0], min_y = ys[0], max_y = ys[0];
    for (size_t i = 1u; i < count; i++) {
        min_x = fminf(min_x, xs[i]);
        max_x = fmaxf(max_x, xs[i]);
        min_y = fminf(min_y, ys[i]);
        max_y = fmaxf(max_y, ys[i]);
    }
    int x0 = clip_lo(min_x), x1 = clip_hi(max_x, c->w);
    int y0 = clip_lo(min_y), y1 = clip_hi(max_y, c->h);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            bool negative = false, positive = false;
            for (size_t i = 0u; i < count; i++) {
                size_t next = i + 1u == count ? 0u : i + 1u;
                float edge = edge_fn(xs[i], ys[i], xs[next], ys[next], px, py);
                negative = negative || edge < 0.0f;
                positive = positive || edge > 0.0f;
            }
            if (!(negative && positive))
                blend_px(c, x, y, rgb, alpha);
        }
    }
}

/* ------------------------------------------------------------------ text */

int sr_text_width(const char *s, int scale)
{
    if (s == NULL) return 0;
    if (scale < 1) scale = 1;
    return (int)strlen(s) * SR_FONT_W * scale;
}

const uint8_t *sr_font_glyph(unsigned char ch)
{
    if (ch < 32u || ch > 126u) ch = (unsigned char)'?';
    return font8x16[ch - 32u];
}

static void draw_glyph(sr_canvas *c, int x, int y, const unsigned char *glyph,
                       uint32_t rgb, float alpha, int scale)
{
    for (int gy = 0; gy < SR_FONT_H; gy++) {
        unsigned row = glyph[gy];
        for (int gx = 0; gx < SR_FONT_W; gx++) {
            if (!((row >> (7 - gx)) & 1u)) continue;
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++)
                    blend_px(c, x + gx * scale + sx, y + gy * scale + sy,
                             rgb, alpha);
        }
    }
}

void sr_text(sr_canvas *c, float x, float y, const char *s,
             uint32_t rgb, float alpha, int scale)
{
    if (!canvas_ok(c) || s == NULL) return;
    if (scale < 1) scale = 1;
    int ix = (int)x, iy = (int)y;
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        draw_glyph(c, ix, iy, sr_font_glyph(ch), rgb, alpha, scale);
        ix += SR_FONT_W * scale;
    }
}

void sr_text_center(sr_canvas *c, float cx, float y, const char *s,
                    uint32_t rgb, float alpha, int scale)
{
    sr_text(c, cx - (float)sr_text_width(s, scale) / 2.0f, y, s,
            rgb, alpha, scale);
}

void sr_text_outlined(sr_canvas *c, float x, float y, const char *s,
                      uint32_t rgb, float alpha, int scale)
{
    sr_text(c, x - 1.0f, y, s, 0x000000u, alpha, scale);
    sr_text(c, x + 1.0f, y, s, 0x000000u, alpha, scale);
    sr_text(c, x, y - 1.0f, s, 0x000000u, alpha, scale);
    sr_text(c, x, y + 1.0f, s, 0x000000u, alpha, scale);
    sr_text(c, x, y, s, rgb, alpha, scale);
}

void sr_text_shadow(sr_canvas *c, float x, float y, const char *s,
                    uint32_t rgb, float alpha, int scale)
{
    if (scale < 1) scale = 1;
    sr_text(c, x + (float)scale, y + (float)scale, s,
            0x000000u, alpha * 0.75f, scale);
    sr_text(c, x, y, s, rgb, alpha, scale);
}

/* ----------------------------------------------------------------- blits */

void sr_blit(sr_canvas *dst, const sr_canvas *src, int x, int y)
{
    if (!canvas_ok(dst) || !canvas_ok(src)) return;
    if (x <= -src->w || y <= -src->h || x >= dst->w || y >= dst->h) return;
    int sx0 = x < 0 ? -x : 0;
    int sy0 = y < 0 ? -y : 0;
    int sx1 = src->w < dst->w - x ? src->w : dst->w - x;
    int sy1 = src->h < dst->h - y ? src->h : dst->h - y;
    for (int sy = sy0; sy < sy1; sy++) {
        const uint32_t *from = &src->px[(size_t)sy * (size_t)src->w + (size_t)sx0];
        uint32_t *to = &dst->px[(size_t)(y + sy) * (size_t)dst->w +
                                (size_t)(x + sx0)];
        if (sx1 > sx0)
            memcpy(to, from, (size_t)(sx1 - sx0) * sizeof(uint32_t));
    }
}

/* Composites one premultiplied source pixel over the destination; ga is the
 * uniform alpha in [0,255].  Matches the sprite compositor the blits were
 * extracted from. */
static void composite_px(uint32_t *to, uint32_t s, int ga)
{
    int sa = ((int)(s >> 24) * ga) / 255;
    if (sa <= 0) return;
    int inv = 255 - sa;
    uint32_t d = *to;
    int r = ((int)((s >> 16) & 255u) * ga) / 255 +
            ((int)((d >> 16) & 255u) * inv) / 255;
    int g = ((int)((s >> 8) & 255u) * ga) / 255 +
            ((int)((d >> 8) & 255u) * inv) / 255;
    int b = ((int)(s & 255u) * ga) / 255 + ((int)(d & 255u) * inv) / 255;
    int a = sa + ((int)(d >> 24) * inv) / 255;
    *to = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
          ((uint32_t)g << 8) | (uint32_t)b;
}

static void composite_tint_px(uint32_t *to, uint32_t s, int ga,
                              uint32_t rgb)
{
    int sa = ((int)(s >> 24) * ga) / 255;
    if (sa <= 0) return;
    int inv = 255 - sa;
    uint32_t d = *to;
    int tr = (int)((rgb >> 16) & 255u);
    int tg = (int)((rgb >> 8) & 255u);
    int tb = (int)(rgb & 255u);
    int r = (tr * sa + (int)((d >> 16) & 255u) * inv) / 255;
    int g = (tg * sa + (int)((d >> 8) & 255u) * inv) / 255;
    int b = (tb * sa + (int)(d & 255u) * inv) / 255;
    int a = sa + ((int)(d >> 24) * inv) / 255;
    *to = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
          ((uint32_t)g << 8) | (uint32_t)b;
}

void sr_blit_alpha(sr_canvas *dst, const sr_canvas *src, int x, int y,
                   float alpha)
{
    if (!canvas_ok(dst) || !canvas_ok(src) || alpha <= 0.0f) return;
    if (x <= -src->w || y <= -src->h || x >= dst->w || y >= dst->h) return;
    int ga = (int)(clampf(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    int sx0 = x < 0 ? -x : 0;
    int sy0 = y < 0 ? -y : 0;
    int sx1 = src->w < dst->w - x ? src->w : dst->w - x;
    int sy1 = src->h < dst->h - y ? src->h : dst->h - y;
    for (int sy = sy0; sy < sy1; sy++) {
        for (int sx = sx0; sx < sx1; sx++) {
            uint32_t s = src->px[(size_t)sy * (size_t)src->w + (size_t)sx];
            composite_px(&dst->px[(size_t)(y + sy) * (size_t)dst->w +
                                  (size_t)(x + sx)], s, ga);
        }
    }
}

void sr_blit_tint(sr_canvas *dst, const sr_canvas *src, int x, int y,
                  uint32_t rgb, float alpha)
{
    if (!canvas_ok(dst) || !canvas_ok(src) || alpha <= 0.0f) return;
    if (x <= -src->w || y <= -src->h || x >= dst->w || y >= dst->h) return;
    int ga = (int)(clampf(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    int sx0 = x < 0 ? -x : 0;
    int sy0 = y < 0 ? -y : 0;
    int sx1 = src->w < dst->w - x ? src->w : dst->w - x;
    int sy1 = src->h < dst->h - y ? src->h : dst->h - y;
    for (int sy = sy0; sy < sy1; sy++) {
        for (int sx = sx0; sx < sx1; sx++) {
            uint32_t s = src->px[(size_t)sy * (size_t)src->w + (size_t)sx];
            uint32_t *to = &dst->px[(size_t)(y + sy) * (size_t)dst->w +
                                    (size_t)(x + sx)];
            composite_tint_px(to, s, ga, rgb);
        }
    }
}

void sr_blit_scaled(sr_canvas *dst, const sr_canvas *src, int x, int y,
                    int w, int h, float alpha)
{
    if (!canvas_ok(dst) || !canvas_ok(src) || w <= 0 || h <= 0 ||
        alpha <= 0.0f) return;
    if (x <= -w || y <= -h || x >= dst->w || y >= dst->h) return;
    int ga = (int)(clampf(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    int dx0 = x < 0 ? -x : 0;
    int dy0 = y < 0 ? -y : 0;
    int dx1 = w < dst->w - x ? w : dst->w - x;
    int dy1 = h < dst->h - y ? h : dst->h - y;
    for (int dy = dy0; dy < dy1; dy++) {
        int sy = (int)((int64_t)dy * src->h / h);
        for (int dx = dx0; dx < dx1; dx++) {
            int sx = (int)((int64_t)dx * src->w / w);
            uint32_t s = src->px[(size_t)sy * (size_t)src->w + (size_t)sx];
            composite_px(&dst->px[(size_t)(y + dy) * (size_t)dst->w +
                                  (size_t)(x + dx)], s, ga);
        }
    }
}

void sr_blit_transformed(sr_canvas *dst, const sr_canvas *src, int x, int y,
                         uint8_t transform, float alpha, bool tint_enabled,
                         uint32_t rgb)
{
    const uint8_t known = SR_TRANSFORM_FLIP_HORIZONTAL |
                          SR_TRANSFORM_FLIP_VERTICAL |
                          SR_TRANSFORM_FLIP_DIAGONAL;
    int output_w;
    int output_h;
    int tx0;
    int ty0;
    int tx1;
    int ty1;
    int ga;

    if (!canvas_ok(dst) || !canvas_ok(src) || !(alpha > 0.0f) ||
        (transform & (uint8_t)~known) != 0u) return;
    output_w = (transform & SR_TRANSFORM_FLIP_DIAGONAL) != 0u
        ? src->h : src->w;
    output_h = (transform & SR_TRANSFORM_FLIP_DIAGONAL) != 0u
        ? src->w : src->h;
    if ((int64_t)x + output_w <= 0 || (int64_t)y + output_h <= 0 ||
        x >= dst->w || y >= dst->h) return;
    ga = (int)(clampf(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    tx0 = x < 0 ? (int)(-(int64_t)x) : 0;
    ty0 = y < 0 ? (int)(-(int64_t)y) : 0;
    tx1 = (int64_t)output_w < (int64_t)dst->w - x
        ? output_w : (int)((int64_t)dst->w - x);
    ty1 = (int64_t)output_h < (int64_t)dst->h - y
        ? output_h : (int)((int64_t)dst->h - y);

    for (int ty = ty0; ty < ty1; ty++) {
        for (int tx = tx0; tx < tx1; tx++) {
            int sx = tx;
            int sy = ty;
            int dx;
            int dy;
            uint32_t s;
            uint32_t *to;

            /* Invert forward diagonal, horizontal, vertical order. */
            if ((transform & SR_TRANSFORM_FLIP_HORIZONTAL) != 0u)
                sx = output_w - 1 - sx;
            if ((transform & SR_TRANSFORM_FLIP_VERTICAL) != 0u)
                sy = output_h - 1 - sy;
            if ((transform & SR_TRANSFORM_FLIP_DIAGONAL) != 0u) {
                int temporary = sx;
                sx = sy;
                sy = temporary;
            }
            dx = (int)((int64_t)x + tx);
            dy = (int)((int64_t)y + ty);
            s = src->px[(size_t)sy * (size_t)src->w + (size_t)sx];
            to = &dst->px[(size_t)dy * (size_t)dst->w + (size_t)dx];
            if (tint_enabled)
                composite_tint_px(to, s, ga, rgb);
            else
                composite_px(to, s, ga);
        }
    }
}

void sr_scale_canvas(sr_canvas *dst, const sr_canvas *src)
{
    if (!canvas_ok(dst)) return;
    sr_clear(dst, 0x000000u);
    if (!canvas_ok(src)) return;
    int dw = dst->w;
    int dh = (int)((int64_t)dw * src->h / src->w);
    if (dh > dst->h) {
        dh = dst->h;
        dw = (int)((int64_t)dh * src->w / src->h);
    }
    if (dw <= 0 || dh <= 0) return;
    int off_x = (dst->w - dw) / 2;
    int off_y = (dst->h - dh) / 2;
    for (int y = 0; y < dh; y++) {
        int sy = (int)((int64_t)y * src->h / dh);
        for (int x = 0; x < dw; x++) {
            int sx = (int)((int64_t)x * src->w / dw);
            uint32_t s = src->px[(size_t)sy * (size_t)src->w + (size_t)sx];
            dst->px[(size_t)(off_y + y) * (size_t)dst->w +
                    (size_t)(off_x + x)] = 0xff000000u | (s & 0x00ffffffu);
        }
    }
}

/* ------------------------------------------------------------------- ppm */

static bool ppm_token(FILE *file, char *buffer, size_t capacity)
{
    int byte;
    size_t length = 0u;
    if (capacity < 2u) return false;
    do {
        byte = fgetc(file);
        if (byte == '#') {
            do byte = fgetc(file); while (byte != '\n' && byte != EOF);
        }
    } while (byte != EOF && isspace((unsigned char)byte));
    if (byte == EOF) return false;
    do {
        if (length + 1u >= capacity) return false;
        buffer[length++] = (char)byte;
        byte = fgetc(file);
    } while (byte != EOF && !isspace((unsigned char)byte));
    buffer[length] = '\0';
    return length > 0u;
}

static bool parse_positive_int(const char *text, int *value)
{
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed <= 0 ||
        parsed > INT_MAX)
        return false;
    *value = (int)parsed;
    return true;
}

bool sr_load_ppm(sr_canvas *c, const char *path)
{
    FILE *file = NULL;
    char token[64];
    int width, height, maxval;
    sr_canvas loaded = {0};
    bool ok = false;

    if (c == NULL) return false;
    c->px = NULL;
    c->w = 0;
    c->h = 0;
    c->owns_px = false;
    if (path == NULL || (file = fopen(path, "rb")) == NULL) return false;
    if (!ppm_token(file, token, sizeof token) || strcmp(token, "P6") != 0 ||
        !ppm_token(file, token, sizeof token) ||
        !parse_positive_int(token, &width) ||
        !ppm_token(file, token, sizeof token) ||
        !parse_positive_int(token, &height) ||
        !ppm_token(file, token, sizeof token) ||
        !parse_positive_int(token, &maxval) || maxval != 255 ||
        !sr_canvas_init(&loaded, width, height))
        goto done;

    size_t pixels = (size_t)width * (size_t)height;
    for (size_t i = 0u; i < pixels; i++) {
        unsigned char bytes[3];
        if (fread(bytes, 1u, sizeof bytes, file) != sizeof bytes) goto done;
        loaded.px[i] = 0xff000000u | sr_rgb(bytes[0], bytes[1], bytes[2]);
    }
    *c = loaded;
    loaded = (sr_canvas){0};
    ok = true;

done:
    sr_canvas_free(&loaded);
    if (fclose(file) != 0) {
        if (ok) sr_canvas_free(c);
        ok = false;
    }
    return ok;
}

bool sr_write_ppm(const sr_canvas *c, const char *path)
{
    if (!canvas_ok(c) || path == NULL) return false;
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    (void)fprintf(file, "P6\n%d %d\n255\n", c->w, c->h);
    size_t n = (size_t)c->w * (size_t)c->h;
    for (size_t i = 0; i < n; i++) {
        uint32_t p = c->px[i];
        unsigned char bytes[3] = {
            (unsigned char)((p >> 16) & 255u),
            (unsigned char)((p >> 8) & 255u),
            (unsigned char)(p & 255u)
        };
        (void)fwrite(bytes, 1, sizeof bytes, file);
    }
    bool ok = !ferror(file);
    if (fclose(file) != 0)
        ok = false;
    return ok;
}
