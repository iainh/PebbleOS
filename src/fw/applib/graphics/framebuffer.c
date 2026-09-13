/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! @file framebuffer.c
//! Bitdepth independent routines for framebuffer.h
//! Bitdepth dependant routines can be found in the 1_bit & 8_bit folders in their
//! respective framebuffer.c files.

#include "applib/graphics/framebuffer.h"
#include "system/passert.h"

#include <stdint.h>

#ifdef CONFIG_COMPOSITOR_DAMAGE_RUST
extern void compositor_damage_reset(uint32_t *tiles, uint16_t word_count);
extern void compositor_damage_mark(uint32_t *tiles, uint16_t word_count, uint16_t tile_columns,
                                   uint16_t tile_rows, int16_t x, int16_t y, int16_t width,
                                   int16_t height);
extern uint16_t compositor_damage_next_row(const uint32_t *tiles, uint16_t word_count,
                                           uint16_t tile_columns, uint16_t tile_rows,
                                           uint16_t start_y, uint16_t height);
#endif

void framebuffer_init(FrameBuffer *fb, const GSize *size) {
  PBL_ASSERTN(!gsize_equal(size, &GSizeZero));
  fb->size = *size;
  framebuffer_reset_dirty(fb);
  // make sure the size is not bigger than the actual buffer size
  PBL_ASSERTN(framebuffer_get_size_bytes(fb) <= FRAMEBUFFER_SIZE_BYTES);
}

GBitmap framebuffer_get_as_bitmap(FrameBuffer *fb, const GSize *size) {
  PBL_ASSERTN(!gsize_equal(size, &GSizeZero));
#if PBL_ROUND
  const GBitmapDataRowInfoInternal *data_row_infos;
  if (fb->size.w == LEGACY_3X_DISP_COLS && fb->size.h == LEGACY_3X_DISP_ROWS) {
    data_row_infos = g_gbitmap_legacy_3x_data_row_infos;
  } else {
    data_row_infos = g_gbitmap_data_row_infos;
  }
#else
  const GBitmapDataRowInfoInternal *data_row_infos = NULL;
#endif

  return (GBitmap){
      .addr = fb->buffer,
      .row_size_bytes = gbitmap_format_get_row_size_bytes(size->w, GBITMAP_NATIVE_FORMAT),
      .info = (BitmapInfo){.format = GBITMAP_NATIVE_FORMAT, .version = GBITMAP_VERSION_CURRENT},
      .bounds = (GRect){GPointZero, *size},
      .data_row_infos = data_row_infos,
  };
}

void framebuffer_dirty_all(FrameBuffer *fb) {
  PBL_ASSERTN(!gsize_equal(&fb->size, &GSizeZero));
  fb->dirty_rect = (GRect){GPointZero, fb->size};
  fb->is_dirty = true;
#ifdef CONFIG_COMPOSITOR_DAMAGE_RUST
  compositor_damage_reset(fb->damage_tiles, FRAMEBUFFER_DAMAGE_TILE_WORDS);
  framebuffer_damage_mark_rect(fb, fb->dirty_rect);
#endif
}

void framebuffer_reset_dirty(FrameBuffer *fb) {
  PBL_ASSERTN(!gsize_equal(&fb->size, &GSizeZero));
  fb->dirty_rect = GRectZero;
  fb->is_dirty = false;
#ifdef CONFIG_COMPOSITOR_DAMAGE_RUST
  compositor_damage_reset(fb->damage_tiles, FRAMEBUFFER_DAMAGE_TILE_WORDS);
#endif
}

bool framebuffer_is_dirty(FrameBuffer *fb) {
  PBL_ASSERTN(!gsize_equal(&fb->size, &GSizeZero));
  return fb->is_dirty;
}

void framebuffer_damage_mark_rect(FrameBuffer *fb, GRect rect) {
#ifdef CONFIG_COMPOSITOR_DAMAGE_RUST
  compositor_damage_mark(fb->damage_tiles, FRAMEBUFFER_DAMAGE_TILE_WORDS,
                         FRAMEBUFFER_DAMAGE_TILE_COLUMNS, FRAMEBUFFER_DAMAGE_TILE_ROWS,
                         rect.origin.x, rect.origin.y, rect.size.w, rect.size.h);
#else
  (void)fb;
  (void)rect;
#endif
}

uint16_t framebuffer_get_next_dirty_row(FrameBuffer *fb, uint16_t start_y) {
#ifdef CONFIG_COMPOSITOR_DAMAGE_RUST
  return compositor_damage_next_row(fb->damage_tiles, FRAMEBUFFER_DAMAGE_TILE_WORDS,
                                    FRAMEBUFFER_DAMAGE_TILE_COLUMNS, FRAMEBUFFER_DAMAGE_TILE_ROWS,
                                    start_y, fb->size.h);
#else
  const uint16_t first_row = fb->dirty_rect.origin.y;
  const uint16_t end_row = first_row + fb->dirty_rect.size.h;
  const uint16_t row = MAX(start_y, first_row);
  return row < end_row ? row : UINT16_MAX;
#endif
}

GSize framebuffer_get_size(FrameBuffer *fb) {
  return fb->size;
}
