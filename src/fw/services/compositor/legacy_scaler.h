/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

typedef struct {
  uint8_t *dst;
  uint16_t dst_len;
  const uint8_t *src;
  uint16_t src_len;
  const uint8_t *src_next;
  uint16_t src_next_len;
  uint16_t dst_start;
  uint16_t dst_count;
  int16_t src_min_x;
  int16_t src_max_x;
  int16_t src_next_min_x;
  int16_t src_next_max_x;
  uint16_t first_coord;
  uint16_t coord_limit;
  uint32_t scale_x;
  uint8_t fy;
  uint8_t flags;
} CompositorScaleRowArgs;

enum { CompositorScaleRowFlag_Bilinear = 1 };

uint16_t compositor_scale_argb2222_row(const CompositorScaleRowArgs *args);

// Runs deterministic 144-pixel bilinear rows and returns an output checksum.
uint32_t compositor_legacy_scaler_benchmark(uint32_t iterations);
