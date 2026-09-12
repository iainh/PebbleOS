/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

// Reproduce with:
//   cmake -S tests -B build-test-bitblt -GNinja -DCONFIG_BITBLT_RUST=ON
//   cmake --build build-test-bitblt --target test_bitblt_benchmark
//   build-test-bitblt/fw/graphics/test_bitblt_benchmark/runme
// Both implementations use their release optimization level and the same process and fixtures.

#include "applib/graphics/bitblt_private.h"

#include "clar.h"
#include "graphics_common_stubs.h"
#include "stubs_applib_resource.h"
#include "stubs_compiled_with_legacy2_sdk.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#define WIDTH 180
#define HEIGHT 180
#define PIXELS (WIDTH * HEIGHT)
#define SAMPLES 9
#define ITERATIONS 80

typedef void (*BlitFunction)(GBitmap *, const GBitmap *, GRect, GPoint, GCompOp, GColor8);

static uint8_t s_source_8[PIXELS];
static uint8_t s_source_packed[PIXELS / 2];
static uint8_t s_dest[PIXELS];
static GColor8 s_palette[16];
static volatile uint32_t s_checksum;

static uint64_t prv_now_ns(void) {
  struct timeval time;
  gettimeofday(&time, NULL);
  return (uint64_t)time.tv_sec * 1000000000ULL + (uint64_t)time.tv_usec * 1000;
}

static void prv_sort(uint64_t values[SAMPLES]) {
  for (int i = 1; i < SAMPLES; ++i) {
    uint64_t value = values[i];
    int j = i;
    while (j > 0 && values[j - 1] > value) {
      values[j] = values[j - 1];
      --j;
    }
    values[j] = value;
  }
}

static uint64_t prv_measure(BlitFunction function, const GBitmap *source, GCompOp mode,
                            GColor8 tint) {
  GBitmap dest = {
      .addr = s_dest,
      .row_size_bytes = WIDTH,
      .info.format = GBitmapFormat8Bit,
      .info.version = GBITMAP_VERSION_CURRENT,
      .bounds = GRect(0, 0, WIDTH, HEIGHT),
  };
  uint64_t samples[SAMPLES];
  function(&dest, source, dest.bounds, GPoint(17, 13), mode, tint);

  for (int sample = 0; sample < SAMPLES; ++sample) {
    memset(s_dest, 0x95 + sample, sizeof(s_dest));
    const uint64_t begin = prv_now_ns();
    for (int iteration = 0; iteration < ITERATIONS; ++iteration) {
      function(&dest, source, dest.bounds, GPoint(17, 13), mode, tint);
    }
    samples[sample] = (prv_now_ns() - begin) / ITERATIONS;
  }
  prv_sort(samples);
  s_checksum += s_dest[(s_checksum + mode * 997) % sizeof(s_dest)];
  return samples[SAMPLES / 2];
}

static void prv_report(const char *name, BlitFunction c_function, BlitFunction rust_function,
                       const GBitmap *source, GCompOp mode, GColor8 tint) {
  const uint64_t c_ns = prv_measure(c_function, source, mode, tint);
  const uint64_t rust_ns = prv_measure(rust_function, source, mode, tint);
  const double speedup = (double)c_ns / rust_ns;
  const double reduction = 100.0 * (c_ns - rust_ns) / c_ns;
  printf("%s,%llu,%llu,%.3f,%.1f%%\n", name, (unsigned long long)c_ns, (unsigned long long)rust_ns,
         speedup, reduction);
}

void test_bitblt_benchmark__initialize(void) {
  for (size_t i = 0; i < sizeof(s_source_8); ++i) {
    s_source_8[i] = (uint8_t)(i * 37 + ((i % 4) << 6));
  }
  for (size_t i = 0; i < sizeof(s_source_packed); ++i) {
    s_source_packed[i] = (uint8_t)(i * 73 + 19);
  }
  for (size_t i = 0; i < sizeof(s_palette) / sizeof(s_palette[0]); ++i) {
    s_palette[i].argb = (uint8_t)(i * 29 + ((i % 4) << 6));
  }
}

void test_bitblt_benchmark__cleanup(void) {}

void test_bitblt_benchmark__watch_sized_paths(void) {
  GBitmap source_8 = {
      .addr = s_source_8,
      .row_size_bytes = WIDTH,
      .info.format = GBitmapFormat8Bit,
      .info.version = GBITMAP_VERSION_CURRENT,
      .bounds = GRect(0, 0, WIDTH, HEIGHT),
  };
  GBitmap source_palette = {
      .addr = s_source_packed,
      .info.version = GBITMAP_VERSION_CURRENT,
      .bounds = GRect(0, 0, WIDTH, HEIGHT),
      .palette = s_palette,
  };
  const GColor8 tint = (GColor8){.argb = 0xa7};

  puts("path,c_ns,rust_ns,speedup,runtime_reduction");
  prv_report("8bit_assign", bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit_c,
             bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit, &source_8, GCompOpAssign, tint);
  prv_report("8bit_set", bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit_c,
             bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit, &source_8, GCompOpSet, tint);
  prv_report("8bit_tint", bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit_c,
             bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit, &source_8, GCompOpTint, tint);
  prv_report("8bit_tint_luminance", bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit_c,
             bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit, &source_8, GCompOpTintLuminance, tint);

  const struct {
    const char *name;
    GBitmapFormat format;
    uint8_t bpp;
  } palette_paths[] = {
      {"palette_1bit_assign", GBitmapFormat1BitPalette, 1},
      {"palette_2bit_assign", GBitmapFormat2BitPalette, 2},
      {"palette_4bit_assign", GBitmapFormat4BitPalette, 4},
  };
  for (size_t i = 0; i < sizeof(palette_paths) / sizeof(palette_paths[0]); ++i) {
    source_palette.info.format = palette_paths[i].format;
    source_palette.row_size_bytes = WIDTH * palette_paths[i].bpp / 8;
    prv_report(palette_paths[i].name, bitblt_bitmap_into_bitmap_tiled_palette_to_8bit_c,
               bitblt_bitmap_into_bitmap_tiled_palette_to_8bit, &source_palette, GCompOpAssign,
               tint);
  }
  source_palette.info.format = GBitmapFormat4BitPalette;
  source_palette.row_size_bytes = WIDTH / 2;
  prv_report("palette_4bit_set", bitblt_bitmap_into_bitmap_tiled_palette_to_8bit_c,
             bitblt_bitmap_into_bitmap_tiled_palette_to_8bit, &source_palette, GCompOpSet, tint);
  prv_report("palette_4bit_tint", bitblt_bitmap_into_bitmap_tiled_palette_to_8bit_c,
             bitblt_bitmap_into_bitmap_tiled_palette_to_8bit, &source_palette, GCompOpTint, tint);
  prv_report("palette_4bit_tint_luminance", bitblt_bitmap_into_bitmap_tiled_palette_to_8bit_c,
             bitblt_bitmap_into_bitmap_tiled_palette_to_8bit, &source_palette, GCompOpTintLuminance,
             tint);
  printf("checksum,%u\n", (unsigned)s_checksum);
}
