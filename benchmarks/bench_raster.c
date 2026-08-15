#define _POSIX_C_SOURCE 200809L

#include "soft_raster.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define FRAME_W 1280
#define FRAME_H 720
#define FRAME_PIXELS ((size_t)FRAME_W * (size_t)FRAME_H)

static double monotonic_seconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1.0;
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static uint64_t canvas_checksum(const sr_canvas *canvas)
{
    uint64_t checksum = 0u;
    const size_t count = (size_t)canvas->w * (size_t)canvas->h;

    for (size_t index = 0u; index < count; index += 4093u)
        checksum = checksum * UINT64_C(0x100000001b3) ^ canvas->px[index];
    return checksum;
}

static bool report_pixels(const char *name, size_t pixels, size_t iterations,
                          double started, uint64_t checksum)
{
    const double elapsed = monotonic_seconds() - started;
    const double total = (double)pixels * (double)iterations;

    if (!(elapsed > 0.0)) return false;
    (void)printf(
        "%s pixels=%zu iterations=%zu megapixels_s=%.2f mean_ms=%.3f "
        "checksum=%016" PRIx64 "\n",
        name, pixels, iterations, total / elapsed / 1000000.0,
        elapsed * 1000.0 / (double)iterations, checksum);
    return true;
}

static bool benchmark_clear(sr_canvas *frame)
{
    const size_t iterations = 200u;
    const double started = monotonic_seconds();

    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < iterations; ++iteration)
        sr_clear(frame, (uint32_t)iteration * UINT32_C(0x010101));
    return report_pixels("clear", FRAME_PIXELS, iterations, started,
                         canvas_checksum(frame));
}

static bool benchmark_pack_rgba(const sr_canvas *frame, uint8_t *rgba)
{
    const size_t iterations = 80u;
    const size_t rgba_byte_count = FRAME_PIXELS * 4u;
    const double started = monotonic_seconds();
    uint64_t checksum;

    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < iterations; ++iteration)
        if (!sr_pack_rgba(frame, rgba, rgba_byte_count)) return false;
    checksum = (uint64_t)rgba[0] << 24 |
               (uint64_t)rgba[rgba_byte_count / 2u] << 16 |
               (uint64_t)rgba[rgba_byte_count - 2u] << 8 |
               rgba[rgba_byte_count - 1u];
    return report_pixels("pack_rgba", FRAME_PIXELS, iterations, started,
                         checksum);
}

static bool benchmark_pack_rgb(const sr_canvas *frame, uint8_t *rgb)
{
    const size_t iterations = 80u;
    const size_t rgb_byte_count = FRAME_PIXELS * 3u;
    const double started = monotonic_seconds();
    uint64_t checksum;

    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < iterations; ++iteration)
        if (!sr_pack_rgb(frame, rgb, rgb_byte_count)) return false;
    checksum = (uint64_t)rgb[0] << 16 |
               (uint64_t)rgb[rgb_byte_count / 2u] << 8 |
               rgb[rgb_byte_count - 1u];
    return report_pixels("pack_rgb", FRAME_PIXELS, iterations, started,
                         checksum);
}

static bool benchmark_fill(sr_canvas *frame)
{
    const size_t iterations = 24u;
    double started = monotonic_seconds();

    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < iterations; ++iteration)
        sr_fill_rect(frame, 0.0f, 0.0f, (float)FRAME_W, (float)FRAME_H,
                     UINT32_C(0x38bdf8), 0.25f);
    if (!report_pixels("fill_rect", FRAME_PIXELS, iterations, started,
                       canvas_checksum(frame))) return false;

    sr_canvas_set_clip(frame, 576, 296, 128, 128);
    started = monotonic_seconds();
    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < 120u; ++iteration)
        sr_fill_rect(frame, 0.0f, 0.0f, (float)FRAME_W, (float)FRAME_H,
                     UINT32_C(0xf59e0b), 0.25f);
    sr_canvas_reset_clip(frame);
    if (!report_pixels("fill_rect_clipped", 128u * 128u, 120u, started,
                       canvas_checksum(frame))) return false;

    started = monotonic_seconds();
    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < 40u; ++iteration)
        sr_fill_rect(frame, 0.25f, 0.25f, (float)FRAME_W - 0.5f,
                     (float)FRAME_H - 0.5f, UINT32_C(0x0f172a), 1.0f);
    return report_pixels("fill_rect_opaque", FRAME_PIXELS, 40u, started,
                         canvas_checksum(frame));
}

static bool benchmark_line(sr_canvas *frame)
{
    const size_t iterations = 24u;
    const double started = monotonic_seconds();

    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < iterations; ++iteration)
        sr_line(frame, 0.0f, 0.0f, (float)(FRAME_W - 1),
                (float)(FRAME_H - 1), 2.0f, UINT32_C(0x34d399), 0.6f, 0, 0);
    return report_pixels("line_diag_bbox", FRAME_PIXELS, iterations, started,
                         canvas_checksum(frame));
}

static bool benchmark_ring(sr_canvas *frame)
{
    const size_t iterations = 80u;
    const size_t bbox_side = 604u;  /* 2 * (r + width / 2 + 1) */
    const double started = monotonic_seconds();

    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < iterations; ++iteration)
        sr_ring(frame, (float)FRAME_W / 2.0f, (float)FRAME_H / 2.0f,
                300.0f, 2.0f, UINT32_C(0xf472b6), 0.8f);
    return report_pixels("ring_bbox", bbox_side * bbox_side, iterations,
                         started, canvas_checksum(frame));
}

static bool benchmark_ellipse(sr_canvas *frame)
{
    const size_t iterations = 24u;
    const size_t bbox = 1202u * 602u;  /* (2 rx + 2) * (2 ry + 2) */
    const double started = monotonic_seconds();

    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < iterations; ++iteration)
        sr_fill_ellipse(frame, (float)FRAME_W / 2.0f, (float)FRAME_H / 2.0f,
                        600.0f, 300.0f, UINT32_C(0xfbbf24), 0.25f);
    return report_pixels("fill_ellipse_bbox", bbox, iterations, started,
                         canvas_checksum(frame));
}

static bool benchmark_text(sr_canvas *frame)
{
    char text[159];
    const size_t iterations = 12u;
    double started;

    for (size_t index = 0u; index < sizeof text - 1u; ++index)
        text[index] = (char)(' ' + 1 + (int)(index % 94u));
    text[sizeof text - 1u] = '\0';
    started = monotonic_seconds();
    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < iterations; ++iteration)
        for (int row = 0; row < FRAME_H / 16; ++row)
            sr_text(frame, 0.0f, (float)(row * 16), text,
                    UINT32_C(0xe2e8f0), 0.9f, 1);
    return report_pixels("text_screen", FRAME_PIXELS, iterations, started,
                         canvas_checksum(frame));
}

static bool benchmark_blits(sr_canvas *frame, sr_canvas *sprite)
{
    double started = monotonic_seconds();

    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < 80u; ++iteration)
        sr_blit_alpha(frame, sprite, 320, 180, 0.75f);
    if (!report_pixels("blit_alpha", (size_t)sprite->w * (size_t)sprite->h,
                       80u, started, canvas_checksum(frame))) return false;

    started = monotonic_seconds();
    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < 16u; ++iteration)
        sr_blit_scaled(frame, sprite, 0, 0, FRAME_W, FRAME_H, 0.75f);
    return report_pixels("blit_scaled", FRAME_PIXELS, 16u, started,
                         canvas_checksum(frame));
}

static bool benchmark_polygon(sr_canvas *frame)
{
    static const float xs[] = {
        64.0f, 640.0f, 1216.0f, 960.0f,
        1120.0f, 640.0f, 160.0f, 320.0f
    };
    static const float ys[] = {
        360.0f, 40.0f, 360.0f, 360.0f,
        680.0f, 520.0f, 680.0f, 360.0f
    };
    const size_t iterations = 80u;
    const double started = monotonic_seconds();

    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < iterations; ++iteration)
        sr_fill_polygon(frame, xs, ys, sizeof xs / sizeof xs[0],
                        UINT32_C(0xa78bfa), 0.25f);
    return report_pixels("fill_polygon_bbox", FRAME_PIXELS, iterations,
                         started, canvas_checksum(frame));
}

static bool benchmark_ppm(sr_canvas *frame)
{
    char path[128];
    const size_t iterations = 4u;
    double started;
    uint64_t checksum = 0u;

    if (snprintf(path, sizeof path, "/tmp/soft-raster-bench-%ld.ppm",
                 (long)getpid()) < 0) return false;
    started = monotonic_seconds();
    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < iterations; ++iteration)
        if (!sr_write_ppm(frame, path)) goto failure;
    if (!report_pixels("write_ppm", FRAME_PIXELS, iterations, started,
                       canvas_checksum(frame))) goto failure;

    started = monotonic_seconds();
    if (started < 0.0) goto failure;
    for (size_t iteration = 0u; iteration < iterations; ++iteration) {
        sr_canvas loaded;
        if (!sr_load_ppm(&loaded, path)) goto failure;
        checksum ^= canvas_checksum(&loaded);
        sr_canvas_free(&loaded);
    }
    if (!report_pixels("load_ppm", FRAME_PIXELS, iterations, started,
                       checksum)) goto failure;
    (void)unlink(path);
    return true;

failure:
    (void)unlink(path);
    return false;
}

int main(void)
{
    sr_canvas frame;
    sr_canvas sprite;
    uint8_t *rgba = NULL;
    uint8_t *rgb = NULL;
    bool succeeded = false;

    if (!sr_canvas_init(&frame, FRAME_W, FRAME_H)) return EXIT_FAILURE;
    if (!sr_canvas_init(&sprite, 320, 180)) goto cleanup_frame;
    rgba = malloc(FRAME_PIXELS * 4u);
    if (rgba == NULL) goto cleanup_sprite;
    rgb = malloc(FRAME_PIXELS * 3u);
    if (rgb == NULL) goto cleanup_rgba;

    sr_clear(&frame, UINT32_C(0x101018));
    sr_fill_circle(&sprite, 160.0f, 90.0f, 88.0f,
                   UINT32_C(0x38bdf8), 0.8f);
    if (!benchmark_clear(&frame) || !benchmark_pack_rgba(&frame, rgba) ||
        !benchmark_fill(&frame) || !benchmark_line(&frame) ||
        !benchmark_ring(&frame) || !benchmark_ellipse(&frame) ||
        !benchmark_text(&frame) || !benchmark_blits(&frame, &sprite) ||
        !benchmark_polygon(&frame) || !benchmark_ppm(&frame) ||
        !benchmark_pack_rgb(&frame, rgb))
        goto cleanup_rgb;
    succeeded = true;

cleanup_rgb:
    free(rgb);
cleanup_rgba:
    free(rgba);
cleanup_sprite:
    sr_canvas_free(&sprite);
cleanup_frame:
    sr_canvas_free(&frame);
    return succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
