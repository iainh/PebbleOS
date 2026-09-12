/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "applib/graphics/gtypes.h"
#include "applib/graphics/text_render.h"

#include "clar.h"

#include "stubs_applib_resource.h"
#include "stubs_app_state.h"
#include "stubs_compiled_with_legacy2_sdk.h"
#include "stubs_heap.h"
#include "stubs_logging.h"
#include "stubs_passert.h"
#include "stubs_resources.h"
#include "stubs_syscalls.h"

#ifdef GLYPH_RASTER_BENCHMARK
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#endif

static GBitmap *s_dest_bitmap;

GBitmap *graphics_context_get_bitmap(GContext *ctx) {
  return s_dest_bitmap;
}

void graphics_context_mark_dirty_rect(GContext *ctx, GRect rect) {}

const GlyphData *text_resources_get_glyph(FontCache *font_cache, const Codepoint codepoint,
                                          FontInfo *fontinfo, int16_t *baseline_adjust_out) {
#ifdef GLYPH_RASTER_BENCHMARK
  typedef struct __attribute__((__packed__)) {
    GlyphHeaderData header;
    uint32_t data[18];
  } BenchmarkGlyph;
  static const BenchmarkGlyph narrow_sparse = {
      .header = {.width_px = 5, .height_px = 18},
      .data = {0x10842108, 0x42108421, 0x08421084},
  };
  static const BenchmarkGlyph wide_dense = {
      .header = {.width_px = 24, .height_px = 24},
      .data =
          {
              0xffffffff,
              0x7ffffffe,
              0xfff00fff,
              0xf0ffff0f,
              0xff00ffff,
              0x0ffff0ff,
              0xffffffff,
              0x7ffffffe,
              0xfff00fff,
              0xf0ffff0f,
              0xff00ffff,
              0x0ffff0ff,
              0xffffffff,
              0x7ffffffe,
              0xfff00fff,
              0xf0ffff0f,
              0xff00ffff,
              0x0ffff0ff,
          },
  };
  return (const GlyphData *)(codepoint == 'i' ? &narrow_sparse : &wide_dense);
#else
  return NULL;
#endif
}

extern int32_t prv_convert_1bit_addr_to_8bit_x(GBitmap *dest_bitmap, uint32_t *block_addr,
                                               int32_t y_offset);

static int32_t prv_get_8bit_x_from_1bit_x(int32_t dest_1bit_x) {
  return (((dest_1bit_x / 32) * 4)) * 8;
}

void test_text_render__convert_1bit_to_8bit_144x168(void) {
  GSize size = GSize(144, 168);
  const int row_1bit_size_words = 1 + (size.w - 1) / 32;

  GBitmap *bitmap = gbitmap_create_blank(size, GBitmapFormat8Bit);
  uintptr_t base = (uintptr_t)bitmap->addr;

  int dest_x = 0;
  int dest_y = 0;
  uint32_t *block_addr = NULL;

  block_addr = (uint32_t *)(uintptr_t)(((dest_y * row_1bit_size_words) + (dest_x / 32)) * 4);
  cl_assert_equal_i(prv_convert_1bit_addr_to_8bit_x(bitmap, block_addr, dest_y),
                    prv_get_8bit_x_from_1bit_x(dest_x));

  dest_x = 50;
  dest_y = 0;
  block_addr = (uint32_t *)(uintptr_t)(((dest_y * row_1bit_size_words) + (dest_x / 32)) * 4);
  cl_assert_equal_i(prv_convert_1bit_addr_to_8bit_x(bitmap, block_addr, dest_y),
                    prv_get_8bit_x_from_1bit_x(dest_x));

  dest_x = 0;
  dest_y = 50;
  block_addr = (uint32_t *)(uintptr_t)(((dest_y * row_1bit_size_words) + (dest_x / 32)) * 4);
  cl_assert_equal_i(prv_convert_1bit_addr_to_8bit_x(bitmap, block_addr, dest_y),
                    prv_get_8bit_x_from_1bit_x(dest_x));

  dest_x = 20;
  dest_y = 100;
  block_addr = (uint32_t *)(uintptr_t)(((dest_y * row_1bit_size_words) + (dest_x / 32)) * 4);
  cl_assert_equal_i(prv_convert_1bit_addr_to_8bit_x(bitmap, block_addr, dest_y),
                    prv_get_8bit_x_from_1bit_x(dest_x));

  gbitmap_destroy(bitmap);
}

void test_text_render__convert_1bit_to_8bit_180x180(void) {
  GSize size = GSize(180, 180);
  const int row_1bit_size_words = 1 + (size.w - 1) / 32;

  GBitmap *bitmap = gbitmap_create_blank(size, GBitmapFormat8Bit);
  uintptr_t base = (uintptr_t)bitmap->addr;

  int dest_x = 0;
  int dest_y = 0;
  uint32_t *block_addr = NULL;

  block_addr = (uint32_t *)(uintptr_t)(((dest_y * row_1bit_size_words) + (dest_x / 32)) * 4);
  cl_assert_equal_i(prv_convert_1bit_addr_to_8bit_x(bitmap, block_addr, dest_y),
                    prv_get_8bit_x_from_1bit_x(dest_x));

  dest_x = 50;
  dest_y = 0;
  block_addr = (uint32_t *)(uintptr_t)(((dest_y * row_1bit_size_words) + (dest_x / 32)) * 4);
  cl_assert_equal_i(prv_convert_1bit_addr_to_8bit_x(bitmap, block_addr, dest_y),
                    prv_get_8bit_x_from_1bit_x(dest_x));

  dest_x = 0;
  dest_y = 50;
  block_addr = (uint32_t *)(uintptr_t)(((dest_y * row_1bit_size_words) + (dest_x / 32)) * 4);
  cl_assert_equal_i(prv_convert_1bit_addr_to_8bit_x(bitmap, block_addr, dest_y),
                    prv_get_8bit_x_from_1bit_x(dest_x));

  dest_x = 20;
  dest_y = 100;
  block_addr = (uint32_t *)(uintptr_t)(((dest_y * row_1bit_size_words) + (dest_x / 32)) * 4);
  cl_assert_equal_i(prv_convert_1bit_addr_to_8bit_x(bitmap, block_addr, dest_y),
                    prv_get_8bit_x_from_1bit_x(dest_x));

  gbitmap_destroy(bitmap);
}

#ifdef GLYPH_RASTER_BENCHMARK
static uint64_t prv_now_ns(void) {
  struct timeval time;
  gettimeofday(&time, NULL);
  return (uint64_t)time.tv_sec * 1000000000 + (uint64_t)time.tv_usec * 1000;
}

static int prv_compare_u64(const void *left, const void *right) {
  const uint64_t a = *(const uint64_t *)left;
  const uint64_t b = *(const uint64_t *)right;
  return (a > b) - (a < b);
}

static void prv_benchmark_glyph(GContext *ctx, Codepoint codepoint, const char *name, GCompOp mode,
                                unsigned int iterations) {
  uint64_t samples[7];
  ctx->draw_state.compositing_mode = mode;
  ctx->draw_state.text_color = mode == GCompOpSet ? (GColor){.argb = 0x55} : GColorBlack;
  render_glyph(ctx, codepoint, NULL, GRect(7, 11, DISP_COLS, DISP_ROWS));
  for (unsigned int sample = 0; sample < sizeof(samples) / sizeof(samples[0]); ++sample) {
    const uint64_t start = prv_now_ns();
    for (unsigned int i = 0; i < iterations; ++i) {
      render_glyph(ctx, codepoint, NULL, GRect(7, 11, DISP_COLS, DISP_ROWS));
    }
    samples[sample] = prv_now_ns() - start;
  }
  qsort(samples, sizeof(samples) / sizeof(samples[0]), sizeof(samples[0]), prv_compare_u64);
  printf("glyph_benchmark,%s,%s,%u,%" PRIu64 ",%.2f\n", name, mode == GCompOpSet ? "set" : "assign",
         iterations, samples[3], (double)samples[3] / iterations);
}
#endif

void test_text_render__benchmark(void) {
#ifdef GLYPH_RASTER_BENCHMARK
  GContext ctx = {0};
  s_dest_bitmap = gbitmap_create_blank(GSize(DISP_COLS, DISP_ROWS), GBitmapFormat8Bit);
  ctx.draw_state.clip_box = GRect(0, 0, DISP_COLS, DISP_ROWS);
  memset(s_dest_bitmap->addr, 0xaa, s_dest_bitmap->row_size_bytes * DISP_ROWS);

  const unsigned int iterations = 200000;
  prv_benchmark_glyph(&ctx, 'i', "narrow_sparse", GCompOpAssign, iterations);
  prv_benchmark_glyph(&ctx, 'M', "wide_dense", GCompOpAssign, iterations);
  prv_benchmark_glyph(&ctx, 'i', "narrow_sparse", GCompOpSet, iterations);
  prv_benchmark_glyph(&ctx, 'M', "wide_dense", GCompOpSet, iterations);

  gbitmap_destroy(s_dest_bitmap);
  s_dest_bitmap = NULL;
#endif
}
