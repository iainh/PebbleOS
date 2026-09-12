/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#define _POSIX_C_SOURCE 200809L
#define ALWAYS_INLINE inline __attribute__((always_inline))

#include "src/fw/applib/graphics/1_bit/bitblt_palette.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef void (*BlitFn)(const PaletteBlitData *data);

typedef struct {
  const char *name;
  int16_t dest_x;
  int16_t width;
  int16_t height;
  int16_t src_width;
  int16_t src_height;
  int16_t src_offset_x;
  int16_t src_offset_y;
  uint8_t bpp;
} Case;

static uint32_t s_dest[57 * 228];
static uint8_t s_src[57 * 31];
static TwoRowLookUp s_look_ups = {
    {.transparent_mask = {1, 0, 1, 1}, .palette_pattern = {0, 0x55555555, 0xaaaaaaaa, 0xffffffff}},
    {.transparent_mask = {1, 0, 1, 1}, .palette_pattern = {0, 0xaaaaaaaa, 0x55555555, 0xffffffff}},
};
static volatile uint32_t s_checksum;

void bitblt_palette_to_1bit_blit_rust(const PaletteBlitData *data);

__attribute__((noinline)) static void prv_c_blit(const PaletteBlitData *data) {
  bitblt_palette_to_1bit_blit_c(data);
}

static uint64_t prv_now_ns(void) {
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);
  return (uint64_t)time.tv_sec * 1000000000 + time.tv_nsec;
}

static uint64_t prv_measure(BlitFn function, const PaletteBlitData *data, unsigned int iterations) {
  memset(s_dest, 0xa5, sizeof(s_dest));
  const uint64_t begin = prv_now_ns();
  for (unsigned int i = 0; i < iterations; ++i) {
    function(data);
  }
  const uint64_t elapsed = prv_now_ns() - begin;
  s_checksum += s_dest[(data->dest_end_y - 1) * data->dest_row_length_words];
  return elapsed;
}

static uint64_t prv_median(uint64_t values[7]) {
  for (unsigned int i = 1; i < 7; ++i) {
    uint64_t value = values[i];
    unsigned int j = i;
    while (j && values[j - 1] > value) {
      values[j] = values[j - 1];
      --j;
    }
    values[j] = value;
  }
  return values[3];
}

int main(void) {
  static const Case cases[] = {
      {"144x168 aligned, 1bpp", 0, 144, 168, 18, 17, 0, 0, 1},
      {"142x168 x=1, 2bpp wrap", 1, 142, 168, 17, 13, 11, 7, 2},
      {"180x180 aligned, 2bpp", 0, 180, 180, 180, 180, 0, 0, 2},
      {"198x228 x=1, 2bpp wrap", 1, 198, 228, 31, 19, 23, 11, 2},
  };
  for (unsigned int i = 0; i < sizeof(s_src); ++i) {
    s_src[i] = (uint8_t)(i * 73 + 19);
  }

  printf("case,c_ns,rust_ns,speedup,reduction_percent\n");
  for (unsigned int index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
    const Case *test = &cases[index];
    const int16_t row_words = (test->dest_x + test->width + 31) / 32;
    const uint16_t src_row_bytes = (test->src_width * test->bpp + 7) / 8;
    PaletteBlitData data = {
        .dest_block_x_begin = s_dest + test->dest_x / 32,
        .src = s_src,
        .look_ups = &s_look_ups,
        .dest_origin_y = 0,
        .dest_end_y = test->height,
        .dest_width = test->width,
        .src_begin_x = 0,
        .src_begin_y = 0,
        .src_end_x = test->src_width,
        .src_end_y = test->src_height,
        .src_offset_x = test->src_offset_x,
        .src_offset_y = test->src_offset_y,
        .src_row_size_bytes = src_row_bytes,
        .dest_row_length_words = row_words,
        .dest_shift_at_line_begin = test->dest_x % 32,
        .num_dest_blocks_per_row = (test->dest_x + test->width + 31) / 32 - test->dest_x / 32,
        .src_bpp = test->bpp,
    };
    const unsigned int iterations = 100000000 / (test->width * test->height);
    uint64_t c_samples[7];
    uint64_t rust_samples[7];
    prv_measure(prv_c_blit, &data, 2);
    prv_measure(bitblt_palette_to_1bit_blit_rust, &data, 2);
    for (unsigned int sample = 0; sample < 7; ++sample) {
      c_samples[sample] = prv_measure(prv_c_blit, &data, iterations);
      rust_samples[sample] = prv_measure(bitblt_palette_to_1bit_blit_rust, &data, iterations);
    }
    const double c_ns = (double)prv_median(c_samples) / iterations;
    const double rust_ns = (double)prv_median(rust_samples) / iterations;
    printf("%s,%.1f,%.1f,%.2f,%.1f\n", test->name, c_ns, rust_ns, c_ns / rust_ns,
           (1.0 - rust_ns / c_ns) * 100.0);
  }
  fprintf(stderr, "checksum=%08" PRIx32 "\n", s_checksum);
  return 0;
}
