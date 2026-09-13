/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "services/compositor/legacy_scaler.h"

#include <string.h>

#define BENCH_WIDTH 144

#ifndef CONFIG_COMPOSITOR_LEGACY_SCALER_RUST
static void prv_scale_reference(const CompositorScaleRowArgs *args) {
  for (uint16_t i = 0; i < args->dst_count; ++i) {
    const uint32_t src_fixed = (uint32_t)(args->first_coord + i) * args->scale_x;
    const uint16_t x = src_fixed >> 16;
    if (x < args->src_min_x || x > args->src_max_x) {
      continue;
    }
    if (!(args->flags & CompositorScaleRowFlag_Bilinear)) {
      args->dst[args->dst_start + i] = args->src[x];
      continue;
    }
    const uint16_t x1 = x + 1 < args->src_len ? x + 1 : args->src_len - 1;
    const uint8_t p00 = args->src[x];
    const uint8_t p10 = x1 <= args->src_max_x ? args->src[x1] : p00;
    const uint8_t p01 =
        x >= args->src_next_min_x && x <= args->src_next_max_x ? args->src_next[x] : p00;
    const uint8_t p11 =
        x1 >= args->src_next_min_x && x1 <= args->src_next_max_x ? args->src_next[x1] : p10;
    const uint8_t fx = (src_fixed >> 12) & 0xf;
    uint8_t result = 0xc0;
    for (uint8_t shift = 0; shift < 6; shift += 2) {
      const uint16_t top = ((p00 >> shift) & 3) * (16 - fx) + ((p10 >> shift) & 3) * fx;
      const uint16_t bottom = ((p01 >> shift) & 3) * (16 - fx) + ((p11 >> shift) & 3) * fx;
      result |= (((top * (16 - args->fy) + bottom * args->fy + 128) >> 8) & 3) << shift;
    }
    args->dst[args->dst_start + i] = result;
  }
}
#endif

uint32_t compositor_legacy_scaler_benchmark(uint32_t iterations) {
  uint8_t src[BENCH_WIDTH];
  uint8_t next[BENCH_WIDTH];
  uint8_t dst[BENCH_WIDTH];
  for (uint16_t x = 0; x < BENCH_WIDTH; ++x) {
    src[x] = 0xc0 | ((x * 29) & 0x3f);
    next[x] = 0xc0 | (((BENCH_WIDTH - x) * 17) & 0x3f);
  }
  const CompositorScaleRowArgs args = {
      .dst = dst,
      .dst_len = BENCH_WIDTH,
      .src = src,
      .src_len = BENCH_WIDTH,
      .src_next = next,
      .src_next_len = BENCH_WIDTH,
      .dst_start = 0,
      .dst_count = BENCH_WIDTH,
      .src_min_x = 0,
      .src_max_x = BENCH_WIDTH - 1,
      .src_next_min_x = 0,
      .src_next_max_x = BENCH_WIDTH - 1,
      .first_coord = 0,
      .coord_limit = BENCH_WIDTH - 1,
      .scale_x = 56320,
      .fy = 7,
      .flags = CompositorScaleRowFlag_Bilinear,
  };
  uint32_t checksum = 2166136261u;
  while (iterations--) {
    memset(dst, 0, sizeof(dst));
#ifdef CONFIG_COMPOSITOR_LEGACY_SCALER_RUST
    compositor_scale_argb2222_row(&args);
#else
    prv_scale_reference(&args);
#endif
    for (uint16_t x = 0; x < BENCH_WIDTH; ++x) {
      checksum = (checksum ^ dst[x]) * 16777619u;
    }
  }
  return checksum;
}
