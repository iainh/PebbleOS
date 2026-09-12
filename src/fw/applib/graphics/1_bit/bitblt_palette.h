/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include "util/graphics.h"

#define BITBLT_PALETTE_ENTRIES 4

typedef struct {
  uint8_t transparent_mask[BITBLT_PALETTE_ENTRIES];
  uint32_t palette_pattern[BITBLT_PALETTE_ENTRIES];
} RowLookUp;

typedef RowLookUp TwoRowLookUp[2];

typedef struct {
  uint32_t *dest_block_x_begin;
  const uint8_t *src;
  const TwoRowLookUp *look_ups;
  int16_t dest_origin_y;
  int16_t dest_end_y;
  int16_t dest_width;
  int16_t src_begin_x;
  int16_t src_begin_y;
  int16_t src_end_x;
  int16_t src_end_y;
  int16_t src_offset_x;
  int16_t src_offset_y;
  uint16_t src_row_size_bytes;
  int16_t dest_row_length_words;
  uint8_t dest_shift_at_line_begin;
  uint8_t num_dest_blocks_per_row;
  uint8_t src_bpp;
} PaletteBlitData;

static inline void bitblt_palette_to_1bit_blit_c(const PaletteBlitData *data) {
  int16_t src_y = data->src_begin_y + data->src_offset_y;
  int16_t dest_y = data->dest_origin_y;

  while (dest_y < data->dest_end_y) {
    if (src_y >= data->src_end_y) {
      src_y = data->src_begin_y;
    }
    uint8_t dest_shift = data->dest_shift_at_line_begin;
    RowLookUp look_up = (*data->look_ups)[dest_y % 2];
    int16_t src_x = data->src_begin_x + data->src_offset_x;
    int16_t row_bits_left = data->dest_width;
    uint32_t *dest_block = data->dest_block_x_begin + (dest_y * data->dest_row_length_words);
    const uint32_t *dest_block_end = dest_block + data->num_dest_blocks_per_row;

    while (dest_block != dest_block_end) {
      uint8_t dest_x = dest_shift;
      while (dest_x < 32 && row_bits_left > 0) {
        if (src_x >= data->src_end_x) {
          src_x = data->src_begin_x;
        }
        uint8_t cindex = raw_image_get_value_for_bitdepth(data->src, src_x, src_y,
                                                          data->src_row_size_bytes, data->src_bpp);
        uint32_t mask = (uint32_t)look_up.transparent_mask[cindex] << dest_x;
        *dest_block = (*dest_block & ~mask) | (look_up.palette_pattern[cindex] & mask);
        dest_x++;
        row_bits_left--;
        src_x++;
      }
      dest_shift = 0;
      dest_block++;
    }
    dest_y++;
    src_y++;
  }
}
