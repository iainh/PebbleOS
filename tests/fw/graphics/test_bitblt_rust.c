/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "applib/graphics/bitblt_private.h"

#include "clar.h"
#include "graphics_common_stubs.h"
#include "stubs_applib_resource.h"
#include "stubs_compiled_with_legacy2_sdk.h"

#include <string.h>

#define DEST_WIDTH 23
#define DEST_HEIGHT 13
#define SRC_STRIDE 16
#define SRC_ROWS 9

static uint8_t s_dest_c[DEST_WIDTH * DEST_HEIGHT];
static uint8_t s_dest_rust[DEST_WIDTH * DEST_HEIGHT];
static uint8_t s_src[SRC_STRIDE * SRC_ROWS];

static GBitmap prv_dest(uint8_t *data) {
  return (GBitmap){
      .addr = data,
      .row_size_bytes = DEST_WIDTH,
      .info.format = GBitmapFormat8Bit,
      .info.version = GBITMAP_VERSION_CURRENT,
      .bounds = GRect(2, 1, 19, 11),
  };
}

static void prv_fill_buffers(void) {
  for (size_t i = 0; i < sizeof(s_src); ++i) {
    s_src[i] = (uint8_t)((i * 73 + 41) | ((i % 4) << 6));
  }
  for (size_t i = 0; i < sizeof(s_dest_c); ++i) {
    s_dest_c[i] = (uint8_t)(i * 29 + 0x51);
  }
  memcpy(s_dest_rust, s_dest_c, sizeof(s_dest_c));
}

static void prv_compare_8bit(const GBitmap *source, GRect dest_rect, GPoint offset, GCompOp mode,
                             GColor8 tint) {
  GBitmap dest_c = prv_dest(s_dest_c);
  GBitmap dest_rust = prv_dest(s_dest_rust);
  bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit_c(&dest_c, source, dest_rect, offset, mode, tint);
  bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit(&dest_rust, source, dest_rect, offset, mode, tint);
  cl_assert_equal_m(s_dest_c, s_dest_rust, sizeof(s_dest_c));
}

static void prv_compare_palette(const GBitmap *source, GRect dest_rect, GPoint offset, GCompOp mode,
                                GColor8 tint) {
  GBitmap dest_c = prv_dest(s_dest_c);
  GBitmap dest_rust = prv_dest(s_dest_rust);
  bitblt_bitmap_into_bitmap_tiled_palette_to_8bit_c(&dest_c, source, dest_rect, offset, mode, tint);
  bitblt_bitmap_into_bitmap_tiled_palette_to_8bit(&dest_rust, source, dest_rect, offset, mode,
                                                  tint);
  cl_assert_equal_m(s_dest_c, s_dest_rust, sizeof(s_dest_c));
}

static void prv_pack(uint8_t *data, int16_t stride, int16_t x, int16_t y, uint8_t bpp,
                     uint8_t value) {
  const uint8_t pixels_per_byte = 8 / bpp;
  const size_t index = y * stride + x / pixels_per_byte;
  const uint8_t shift = 8 - bpp * (1 + x % pixels_per_byte);
  const uint8_t mask = ((1 << bpp) - 1) << shift;
  data[index] = (data[index] & ~mask) | ((value << shift) & mask);
}

void test_bitblt_rust__initialize(void) {
  prv_fill_buffers();
}

void test_bitblt_rust__cleanup(void) {}

void test_bitblt_rust__8bit_asymmetric_bounds_offsets_wrap_and_modes(void) {
  GBitmap source = {
      .addr = s_src,
      .row_size_bytes = SRC_STRIDE,
      .info.format = GBitmapFormat8Bit,
      .info.version = GBITMAP_VERSION_CURRENT,
      .bounds = GRect(3, 2, 7, 5),
  };
  const GPoint offsets[] = {GPoint(-11, -7), GPoint(15, 8), GPoint(6, -1)};

  for (GCompOp mode = GCompOpAssign; mode <= GCompOpTintLuminance; ++mode) {
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
      prv_fill_buffers();
      prv_compare_8bit(&source, GRect(2, 1, 19, 11), offsets[i], mode, (GColor8){.argb = 0x9d});
    }
  }
}

void test_bitblt_rust__8bit_circular_source_and_destination_clipping(void) {
  GBitmapDataRowInfoInternal source_rows[SRC_ROWS] = {};
  for (int y = 2; y < 7; ++y) {
    source_rows[y] = (GBitmapDataRowInfoInternal){
        .offset = y * SRC_STRIDE,
        .min_x = 3 + (y & 1),
        .max_x = 9 - ((y + 1) & 1),
    };
  }
  GBitmap source = {
      .addr = s_src,
      .info.format = GBitmapFormat8BitCircular,
      .info.version = GBITMAP_VERSION_CURRENT,
      .bounds = GRect(3, 2, 7, 5),
      .data_row_infos = source_rows,
  };

  GBitmapDataRowInfoInternal dest_rows[DEST_HEIGHT] = {};
  for (int y = 1; y < 12; ++y) {
    dest_rows[y] = (GBitmapDataRowInfoInternal){
        .offset = y * DEST_WIDTH,
        .min_x = 2 + (y % 3),
        .max_x = 20 - ((y + 1) % 3),
    };
  }

  for (GCompOp mode = GCompOpAssign; mode <= GCompOpTintLuminance; ++mode) {
    prv_fill_buffers();
    GBitmap dest_c = prv_dest(s_dest_c);
    GBitmap dest_rust = prv_dest(s_dest_rust);
    dest_c.info.format = GBitmapFormat8BitCircular;
    dest_c.data_row_infos = dest_rows;
    dest_rust.info.format = GBitmapFormat8BitCircular;
    dest_rust.data_row_infos = dest_rows;
    bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit_c(&dest_c, &source, GRect(1, 1, 21, 11),
                                                   GPoint(12, -8), mode, (GColor8){.argb = 0x76});
    bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit(&dest_rust, &source, GRect(1, 1, 21, 11),
                                                 GPoint(12, -8), mode, (GColor8){.argb = 0x76});
    cl_assert_equal_m(s_dest_c, s_dest_rust, sizeof(s_dest_c));
  }
}

void test_bitblt_rust__palette_depths_alpha_tint_wrap_and_clipping(void) {
  const struct {
    GBitmapFormat format;
    uint8_t bpp;
  } formats[] = {
      {GBitmapFormat1BitPalette, 1},
      {GBitmapFormat2BitPalette, 2},
      {GBitmapFormat4BitPalette, 4},
  };
  const GCompOp modes[] = {
      GCompOpAssign,
      GCompOpSet,
      GCompOpTint,
      GCompOpTintLuminance,
  };
  GColor8 palette[16];
  for (size_t i = 0; i < sizeof(palette) / sizeof(palette[0]); ++i) {
    palette[i].argb = (uint8_t)(i * 37 + ((i % 4) << 6));
  }

  for (size_t format = 0; format < sizeof(formats) / sizeof(formats[0]); ++format) {
    memset(s_src, 0xa5, sizeof(s_src));
    for (int y = 2; y < 7; ++y) {
      for (int x = 3; x < 10; ++x) {
        prv_pack(s_src, SRC_STRIDE, x, y, formats[format].bpp,
                 (x * 3 + y * 5) & ((1 << formats[format].bpp) - 1));
      }
    }
    GBitmap source = {
        .addr = s_src,
        .row_size_bytes = SRC_STRIDE,
        .info.format = formats[format].format,
        .info.version = GBITMAP_VERSION_CURRENT,
        .bounds = GRect(3, 2, 7, 5),
        .palette = palette,
    };
    for (size_t mode = 0; mode < sizeof(modes) / sizeof(modes[0]); ++mode) {
      prv_fill_buffers();
      prv_compare_palette(&source, GRect(4, 2, 16, 9), GPoint(-10, 11), modes[mode],
                          (GColor8){.argb = 0xa7});
    }
  }
}

void test_bitblt_rust__palette_circular_destination_clipping(void) {
  GColor8 palette[16];
  for (size_t i = 0; i < sizeof(palette) / sizeof(palette[0]); ++i) {
    palette[i].argb = (uint8_t)(i * 31 + ((3 - i % 4) << 6));
  }
  memset(s_src, 0x5a, sizeof(s_src));
  for (int y = 2; y < 7; ++y) {
    for (int x = 3; x < 10; ++x) {
      prv_pack(s_src, SRC_STRIDE, x, y, 4, (x * 5 + y * 7) & 0xf);
    }
  }

  GBitmap source = {
      .addr = s_src,
      .row_size_bytes = SRC_STRIDE,
      .info.format = GBitmapFormat4BitPalette,
      .info.version = GBITMAP_VERSION_CURRENT,
      .bounds = GRect(3, 2, 7, 5),
      .palette = palette,
  };

  GBitmapDataRowInfoInternal dest_rows[DEST_HEIGHT] = {};
  for (int y = 1; y < 12; ++y) {
    dest_rows[y] = (GBitmapDataRowInfoInternal){
        .offset = y * DEST_WIDTH,
        .min_x = 2 + (y % 3),
        .max_x = 20 - ((y + 1) % 3),
    };
  }
  const GCompOp modes[] = {
      GCompOpAssign,
      GCompOpSet,
      GCompOpTint,
      GCompOpTintLuminance,
  };
  for (size_t mode = 0; mode < sizeof(modes) / sizeof(modes[0]); ++mode) {
    prv_fill_buffers();
    GBitmap dest_c = prv_dest(s_dest_c);
    GBitmap dest_rust = prv_dest(s_dest_rust);
    dest_c.info.format = GBitmapFormat8BitCircular;
    dest_c.data_row_infos = dest_rows;
    dest_rust.info.format = GBitmapFormat8BitCircular;
    dest_rust.data_row_infos = dest_rows;
    bitblt_bitmap_into_bitmap_tiled_palette_to_8bit_c(&dest_c, &source, GRect(1, 1, 21, 11),
                                                      GPoint(-12, 9), modes[mode],
                                                      (GColor8){.argb = 0xb5});
    bitblt_bitmap_into_bitmap_tiled_palette_to_8bit(&dest_rust, &source, GRect(1, 1, 21, 11),
                                                    GPoint(-12, 9), modes[mode],
                                                    (GColor8){.argb = 0xb5});
    cl_assert_equal_m(s_dest_c, s_dest_rust, sizeof(s_dest_c));
  }
}
