/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "applib/graphics/framebuffer.h"
#include "applib/graphics/graphics.h"
#include "applib/graphics/gtypes.h"
#include "pbl/util/trig.h"

#include "clar.h"

#include <string.h>
#include <sys/time.h>

#include "graphics_common_stubs.h"
#include "stubs_applib_resource.h"
#include "test_graphics.h"

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

extern GBitmap *prv_gbitmap_create_blank_internal_no_platform_checks(GSize size,
                                                                     GBitmapFormat format);
extern void graphics_draw_rotated_bitmap_c(GContext *ctx, GBitmap *src, GPoint src_ic, int rotation,
                                           GPoint dest_ic);

static FrameBuffer s_c_framebuffer;
static FrameBuffer s_rust_framebuffer;

void test_graphics_rotated_bitmap_differential__initialize(void) {
  framebuffer_init(&s_c_framebuffer, &(GSize){DISP_COLS, DISP_ROWS});
  framebuffer_init(&s_rust_framebuffer, &(GSize){DISP_COLS, DISP_ROWS});
}

void test_graphics_rotated_bitmap_differential__cleanup(void) {}

static void prv_fill_source(GBitmap *bitmap) {
  const size_t size = bitmap->row_size_bytes * bitmap->bounds.size.h;
  for (size_t i = 0; i < size; ++i) {
    ((uint8_t *)bitmap->addr)[i] = (uint8_t)(i * 73 + i / 7 + 0x35);
  }
  if (bitmap->info.format >= GBitmapFormat1BitPalette &&
      bitmap->info.format <= GBitmapFormat4BitPalette) {
    const unsigned int entries = 1U << gbitmap_get_bits_per_pixel(bitmap->info.format);
    for (unsigned int i = 0; i < entries; ++i) {
      bitmap->palette[i].argb = (uint8_t)((i * 29) ^ (i % 4 << 6));
    }
  }
}

static void prv_prepare_context(GContext *ctx, FrameBuffer *framebuffer, GRect clip,
                                GPoint drawing_origin, GCompOp mode, GColor tint) {
  test_graphics_context_init(ctx, framebuffer);
  memset(ctx->dest_bitmap.addr, 0x5a, framebuffer_get_size_bytes(framebuffer));
  ctx->draw_state.clip_box = clip;
  ctx->draw_state.drawing_box =
      (GRect){.origin = drawing_origin, .size = GSize(DISP_COLS, DISP_ROWS)};
  ctx->draw_state.compositing_mode = mode;
  ctx->draw_state.tint_color = tint;
}

static void prv_compare_case(GBitmapFormat format, GSize size, GPoint source_pivot,
                             GPoint destination_pivot, int degrees, GRect clip,
                             GPoint drawing_origin, GCompOp mode, GColor tint) {
  GBitmap *source = prv_gbitmap_create_blank_internal_no_platform_checks(size, format);
  cl_assert(source != NULL);
  prv_fill_source(source);

  GContext c_context;
  GContext rust_context;
  prv_prepare_context(&c_context, &s_c_framebuffer, clip, drawing_origin, mode, tint);
  prv_prepare_context(&rust_context, &s_rust_framebuffer, clip, drawing_origin, mode, tint);

  graphics_draw_rotated_bitmap_c(&c_context, source, source_pivot, DEG_TO_TRIGANGLE(degrees),
                                 destination_pivot);
  graphics_draw_rotated_bitmap(&rust_context, source, source_pivot, DEG_TO_TRIGANGLE(degrees),
                               destination_pivot);

  cl_assert_equal_i(framebuffer_get_size_bytes(&s_c_framebuffer),
                    framebuffer_get_size_bytes(&s_rust_framebuffer));
  cl_assert_equal_i(memcmp(&s_c_framebuffer, &s_rust_framebuffer, sizeof(FrameBuffer)), 0);
  gbitmap_destroy(source);
}

void test_graphics_rotated_bitmap_differential__asymmetric_pivots_clipping_and_rotations(void) {
  static const int rotations[] = {0, 1, 2, 37, 45, 91, 179, 180, 271, 315};
#if CONFIG_SCREEN_COLOR_DEPTH_BITS == 1
  static const GCompOp modes[] = {
      GCompOpAssign, GCompOpAssignInverted, GCompOpOr, GCompOpAnd, GCompOpClear, GCompOpSet,
  };
  for (size_t mode = 0; mode < ARRAY_LENGTH(modes); ++mode) {
    for (size_t rotation = 0; rotation < ARRAY_LENGTH(rotations); ++rotation) {
      prv_compare_case(GBitmapFormat1Bit, GSize(37, 23), GPoint(5, 17), GPoint(61, 79),
                       rotations[rotation], GRect(11, 13, 97, 121), GPoint(3, -4), modes[mode],
                       GColorWhite);
    }
  }
#else
  static const GBitmapFormat formats[] = {
      GBitmapFormat1Bit,        GBitmapFormat8Bit,        GBitmapFormat1BitPalette,
      GBitmapFormat2BitPalette, GBitmapFormat4BitPalette,
  };
  static const GCompOp modes[] = {GCompOpAssign, GCompOpSet, GCompOpOr};
  for (size_t format = 0; format < ARRAY_LENGTH(formats); ++format) {
    for (size_t mode = 0; mode < ARRAY_LENGTH(modes); ++mode) {
      for (size_t rotation = 0; rotation < ARRAY_LENGTH(rotations); ++rotation) {
        prv_compare_case(formats[format], GSize(37, 23), GPoint(5, 17), GPoint(61, 79),
                         rotations[rotation], GRect(11, 13, 97, 121), GPoint(3, -4), modes[mode],
                         GColorRed);
      }
    }
  }
#endif
}

void test_graphics_rotated_bitmap_differential__off_bitmap_pivot(void) {
  prv_compare_case(GBitmapFormat1Bit, GSize(19, 31), GPoint(-7, 39), GPoint(72, 84), 123,
                   GRect(0, 0, DISP_COLS, DISP_ROWS), GPointZero, GCompOpAssign, GColorClear);
}

static uint64_t prv_time_draws(void (*draw)(GContext *, GBitmap *, GPoint, int, GPoint),
                               GContext *ctx, GBitmap *source, unsigned int iterations) {
  struct timeval start;
  struct timeval end;
  gettimeofday(&start, NULL);
  for (unsigned int i = 0; i < iterations; ++i) {
    draw(ctx, source, GPoint(17, 29), DEG_TO_TRIGANGLE(37), GPoint(72, 84));
    draw(ctx, source, GPoint(17, 29), DEG_TO_TRIGANGLE(91), GPoint(72, 84));
    draw(ctx, source, GPoint(17, 29), DEG_TO_TRIGANGLE(180), GPoint(72, 84));
  }
  gettimeofday(&end, NULL);
  return (uint64_t)(end.tv_sec - start.tv_sec) * 1000000 + end.tv_usec - start.tv_usec;
}

void test_graphics_rotated_bitmap_differential__benchmark(void) {
#if CONFIG_SCREEN_COLOR_DEPTH_BITS == 1
  const GBitmapFormat format = GBitmapFormat1Bit;
  const GCompOp mode = GCompOpAssign;
#else
  const GBitmapFormat format = GBitmapFormat8Bit;
  const GCompOp mode = GCompOpSet;
#endif
  GBitmap *source = prv_gbitmap_create_blank_internal_no_platform_checks(GSize(60, 60), format);
  cl_assert(source != NULL);
  prv_fill_source(source);
  GContext c_context;
  GContext rust_context;
  prv_prepare_context(&c_context, &s_c_framebuffer, GRect(0, 0, DISP_COLS, DISP_ROWS), GPointZero,
                      mode, GColorClear);
  prv_prepare_context(&rust_context, &s_rust_framebuffer, GRect(0, 0, DISP_COLS, DISP_ROWS),
                      GPointZero, mode, GColorClear);

  const unsigned int iterations = 200;
  uint64_t best_c = UINT64_MAX;
  uint64_t best_rust = UINT64_MAX;
  for (unsigned int trial = 0; trial < 5; ++trial) {
    const uint64_t c =
        prv_time_draws(graphics_draw_rotated_bitmap_c, &c_context, source, iterations);
    const uint64_t rust =
        prv_time_draws(graphics_draw_rotated_bitmap, &rust_context, source, iterations);
    best_c = MIN(best_c, c);
    best_rust = MIN(best_rust, rust);
  }
  printf(
      "ROTATED_BITMAP_BENCHMARK platform=%s size=60x60 rotations=37,91,180 "
      "iterations=%u c_us=%llu rust_us=%llu\n",
      PLATFORM_NAME, iterations, (unsigned long long)best_c, (unsigned long long)best_rust);
  gbitmap_destroy(source);
}
