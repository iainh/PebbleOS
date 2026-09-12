/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "../bitblt_private.h"
#include "bitblt_palette.h"

#include "applib/app_logging.h"
#include "applib/graphics/graphics.h"
#include "applib/graphics/graphics_private.h"
#include "applib/graphics/gtypes.h"
#include <pbl/logging/logging.h>
#include "system/passert.h"
#include "util/graphics.h"
#include "pbl/util/size.h"

#if CONFIG_BITBLT_PALETTE_RUST
void bitblt_palette_to_1bit_blit_rust(const PaletteBlitData *data);
#endif

T_STATIC void prv_apply_tint_color(GColor *color, GColor tint_color) {
  // tint_color.a is always 0 or 3
  if (tint_color.a != 0) {
    tint_color.a = (*color).a;
    *color = tint_color;
  }
}

T_STATIC void prv_calc_two_row_look_ups(TwoRowLookUp *look_up, GCompOp compositing_mode,
                                        const GColor8 *palette, uint8_t num_entries,
                                        GColor tint_color) {
  for (unsigned int palette_index = 0; palette_index < num_entries; palette_index++) {
    GColor color = palette[palette_index];
    // gcolor_get_grayscale will convert any color with an alpha less than 2 to clear
    // alpha should be ignored in the case of GCompOpAssign so the alpha is set to 3
    if (compositing_mode == GCompOpAssign) {
      color.a = 3;
    } else if (compositing_mode == GCompOpTint) {
      prv_apply_tint_color(&color, tint_color);
    } else if (compositing_mode == GCompOpTintLuminance) {
      color = gcolor_tint_using_luminance_and_multiply_alpha(color, tint_color);
    }
    color = gcolor_get_grayscale(color);
    for (unsigned int row_number = 0; row_number < ARRAY_LENGTH(*look_up); row_number++) {
      (*look_up)[row_number].palette_pattern[palette_index] =
          graphics_private_get_1bit_grayscale_pattern(color, row_number);
      (*look_up)[row_number].transparent_mask[palette_index] =
          gcolor_is_transparent(color) ? false : true;
    }
  }
}

static bool prv_prepare_palette_blit(PaletteBlitData *data, TwoRowLookUp *look_ups,
                                     GBitmap *dest_bitmap, const GBitmap *src_bitmap,
                                     GRect dest_rect, GPoint src_origin_offset,
                                     GCompOp compositing_mode, GColor tint_color) {
  if (!src_bitmap->palette) {
    return false;
  }
  switch (compositing_mode) {
    case GCompOpAssign:
    case GCompOpSet:
    case GCompOpTint:
    case GCompOpTintLuminance:
      break;
    default:
      PBL_LOG_DBG("Only the assign, set and tint modes are allowed for palettized bitmaps");
      return false;
  }

  const int8_t dest_begin_x = (dest_rect.origin.x / 32);
  const int dest_row_length_words = (dest_bitmap->row_size_bytes / 4);
  const int16_t dest_end_x = grect_get_max_x(&dest_rect);
  const uint8_t num_dest_blocks_per_row =
      (dest_end_x / 32) + ((dest_end_x % 32) ? 1 : 0) - dest_begin_x;
  const uint8_t src_bpp = gbitmap_get_bits_per_pixel(gbitmap_get_format(src_bitmap));
  const uint8_t src_palette_size = 1 << src_bpp;
  PBL_ASSERTN(src_palette_size <= BITBLT_PALETTE_ENTRIES);

  prv_calc_two_row_look_ups(look_ups, compositing_mode, src_bitmap->palette, src_palette_size,
                            tint_color);
  *data = (PaletteBlitData){
      .dest_block_x_begin = ((uint32_t *)dest_bitmap->addr) + dest_begin_x,
      .src = src_bitmap->addr,
      .look_ups = look_ups,
      .dest_origin_y = dest_rect.origin.y,
      .dest_end_y = grect_get_max_y(&dest_rect),
      .dest_width = dest_rect.size.w,
      .src_begin_x = src_bitmap->bounds.origin.x,
      .src_begin_y = src_bitmap->bounds.origin.y,
      .src_end_x = grect_get_max_x(&src_bitmap->bounds),
      .src_end_y = grect_get_max_y(&src_bitmap->bounds),
      .src_offset_x = src_origin_offset.x,
      .src_offset_y = src_origin_offset.y,
      .src_row_size_bytes = src_bitmap->row_size_bytes,
      .dest_row_length_words = dest_row_length_words,
      .dest_shift_at_line_begin = dest_rect.origin.x % 32,
      .num_dest_blocks_per_row = num_dest_blocks_per_row,
      .src_bpp = src_bpp,
  };
  return true;
}

void bitblt_bitmap_into_bitmap_tiled_palette_to_1bit_c(GBitmap *dest_bitmap,
                                                       const GBitmap *src_bitmap, GRect dest_rect,
                                                       GPoint src_origin_offset,
                                                       GCompOp compositing_mode,
                                                       GColor tint_color) {
  PaletteBlitData data;
  TwoRowLookUp look_ups;
  if (prv_prepare_palette_blit(&data, &look_ups, dest_bitmap, src_bitmap, dest_rect,
                               src_origin_offset, compositing_mode, tint_color)) {
    bitblt_palette_to_1bit_blit_c(&data);
  }
}

void bitblt_bitmap_into_bitmap_tiled_palette_to_1bit(GBitmap *dest_bitmap,
                                                     const GBitmap *src_bitmap, GRect dest_rect,
                                                     GPoint src_origin_offset,
                                                     GCompOp compositing_mode, GColor tint_color) {
  PaletteBlitData data;
  TwoRowLookUp look_ups;
  if (!prv_prepare_palette_blit(&data, &look_ups, dest_bitmap, src_bitmap, dest_rect,
                                src_origin_offset, compositing_mode, tint_color)) {
    return;
  }
#if CONFIG_BITBLT_PALETTE_RUST
  bitblt_palette_to_1bit_blit_rust(&data);
#else
  bitblt_palette_to_1bit_blit_c(&data);
#endif
}

void bitblt_bitmap_into_bitmap_tiled(GBitmap *dest_bitmap, const GBitmap *src_bitmap,
                                     GRect dest_rect, GPoint src_origin_offset,
                                     GCompOp compositing_mode, GColor8 tint_color) {
  if (bitblt_compositing_mode_is_noop(compositing_mode, tint_color)) {
    return;
  }

  GBitmapFormat src_fmt = gbitmap_get_format(src_bitmap);
  GBitmapFormat dest_fmt = gbitmap_get_format(dest_bitmap);
  if (dest_fmt != GBitmapFormat1Bit) {
    return;
  }

  switch (src_fmt) {
    case GBitmapFormat1Bit:
      bitblt_bitmap_into_bitmap_tiled_1bit_to_1bit(dest_bitmap, src_bitmap, dest_rect,
                                                   src_origin_offset, compositing_mode, tint_color);
      break;
    case GBitmapFormat1BitPalette:
    case GBitmapFormat2BitPalette:
      bitblt_bitmap_into_bitmap_tiled_palette_to_1bit(
          dest_bitmap, src_bitmap, dest_rect, src_origin_offset, compositing_mode, tint_color);
      break;
    default:
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Only 1 and 2 bit palettized images can be displayed.");
      return;
  }
}
