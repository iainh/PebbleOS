/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "applib/graphics/graphics.h"
#include "applib/graphics/graphics_private.h"
#include "applib/graphics/bitblt.h"
#include "applib/graphics/bitblt_private.h"
#include "applib/graphics/1_bit/framebuffer.h"
#include "applib/graphics/gtypes.h"

#include "clar.h"
#include "util.h"

#include <string.h>
#include <stdio.h>

// Stubs
////////////////////////////////////
#include "graphics_common_stubs.h"
#include "stubs_applib_resource.h"
#include "test_graphics.h"

extern void prv_apply_tint_color(GColor *color, GColor tint_color);

static uint8_t s_dest_data[100 * 100];
static GBitmap s_dest_bitmap = {.addr = s_dest_data,
                                .row_size_bytes = 16,
                                .info.format = GBitmapFormat1Bit,
                                .info.version = GBITMAP_VERSION_CURRENT,
                                .bounds = GRect(0, 0, 100, 100)};
// Tests
////////////////////////////////////

// setup and teardown
void test_bitblt_palette_1bit__initialize(void) {
  // Set dest bitmap to white
  memset(s_dest_data, 0b11111111, sizeof(s_dest_data));
}

void test_bitblt_palette_1bit__cleanup(void) {}

// Test images reside in "tests/fw/graphics/test_images/".
// The wscript will convert them from PNGs in that directory to PBIs in the build directory.

// Tests assign, from same size to same size.
// Setup:
//   - Source is 50x50, the left half is semi transparent orange and the right half is orange.
//   - Dest is 100x100, white.
// Result:
//   - All dithered gray.
void test_bitblt_palette_1bit__1bit_palette_to_1bit_assign(void) {
  GBitmap *src_bitmap = get_gbitmap_from_pbi("test_bitblt_palette_1bit__1bit_palette_to_1bit.pbi");

  bitblt_bitmap_into_bitmap(&s_dest_bitmap, src_bitmap, GPointZero, GCompOpAssign, GColorWhite);
  cl_assert(gbitmap_pbi_eq(&s_dest_bitmap,
                           "test_bitblt_palette_1bit__1bit_palette_to_1bit_assign-expect.pbi"));

  gbitmap_destroy(src_bitmap);
}

// Test images reside in "tests/fw/graphics/test_images/".
// The wscript will convert them from PNGs in that directory to PBIs in the build directory.

// Tests assign, from same size to same size.
// Setup:
//   - Source is 50x50, the left half is semi transparent orange and the right half is orange.
//   - Dest is 100x100, white.
// Result:
//   - The left half will be white and the right half will be dithered gray.
void test_bitblt_palette_1bit__1bit_palette_to_1bit_set(void) {
  GBitmap *src_bitmap = get_gbitmap_from_pbi("test_bitblt_palette_1bit__1bit_palette_to_1bit.pbi");

  bitblt_bitmap_into_bitmap(&s_dest_bitmap, src_bitmap, GPointZero, GCompOpSet, GColorWhite);
  cl_assert(gbitmap_pbi_eq(&s_dest_bitmap,
                           "test_bitblt_palette_1bit__1bit_palette_to_1bit_set-expect.pbi"));

  gbitmap_destroy(src_bitmap);
}

// Tests assign, from same size to same size.
// Setup:
//   - Source is 50x50, alternating lines between orange and blue for the top half.
//     The bottom half is a diagonal orange line over blue.
//     The left half is semi-transparent and the right half is completely opaque
//   - Dest is 100x100, white.
// Result:
//   - The top half will be alternating between dithered gray and black lines
//     The bottom half consists of a diagonal white line  on a black background
void test_bitblt_palette_1bit__2bit_palette_to_1bit_assign(void) {
  GBitmap *src_bitmap = get_gbitmap_from_pbi("test_bitblt_palette_1bit__2bit_palette_to_1bit.pbi");

  bitblt_bitmap_into_bitmap(&s_dest_bitmap, src_bitmap, GPointZero, GCompOpAssign, GColorWhite);
  cl_assert(
      gbitmap_pbi_eq(&s_dest_bitmap, "test_bitblt_palette_1bit__2bit_palette_to_1bit-expect.pbi"));

  gbitmap_destroy(src_bitmap);
}

// Tests assign, from same size to same size.
// Setup:
//   - Source is 50x50, alternating lines between orange and blue for the top half.
//     The bottom half is a diagonal orange line over blue.
//     The left half is semi-transparent and the right half is completely opaque
//   - Dest is 100x100, white.
// Result:
//   - The top right half will be alternating between dithered gray and black lines
//     The bottom right half consists of a diagonal white line  on a black background
//     The left half will be completely white
void test_bitblt_palette_1bit__2bit_palette_to_1bit_set(void) {
  GBitmap *src_bitmap = get_gbitmap_from_pbi("test_bitblt_palette_1bit__2bit_palette_to_1bit.pbi");

  bitblt_bitmap_into_bitmap(&s_dest_bitmap, src_bitmap, GPointZero, GCompOpSet, GColorWhite);
  cl_assert(gbitmap_pbi_eq(&s_dest_bitmap,
                           "test_bitblt_palette_1bit__2bit_palette_to_1bit_set-expect.pbi"));

  gbitmap_destroy(src_bitmap);
}

// Tests assign, from same size to same size.
// Setup:
//   - Source is 50x50, alternating lines between orange and blue for the top half.
//     The bottom half is a diagonal orange line over blue.
//     The left half is semi-transparent and the right half is completely opaque
//   - Dest is 50x50, white.
// Result:
//   - The image described will be tiled in each of the four corners
//     The top right half will be alternating between dithered gray and black lines
//     The bottom right half consists of a diagonal white line  on a black background
//     The left half will be completely white
void test_bitblt_palette_1bit__2bit_palette_to_1bit_wrap(void) {
  GBitmap *src_bitmap = get_gbitmap_from_pbi("test_bitblt_palette_1bit__2bit_palette_to_1bit.pbi");

  bitblt_bitmap_into_bitmap_tiled(&s_dest_bitmap, src_bitmap, s_dest_bitmap.bounds, GPointZero,
                                  GCompOpAssign, GColorWhite);
  cl_assert(gbitmap_pbi_eq(&s_dest_bitmap,
                           "test_bitblt_palette_1bit__2bit_palette_to_1bit_wrap-expect.pbi"));

  gbitmap_destroy(src_bitmap);
}

// Tests assign, from same size to same size.
// Setup:
//   - Source is 50x50, alternating lines between orange and blue for the top half.
//     The bottom half is a diagonal orange line over blue.
//     The left half is semi-transparent and the right half is completely opaque
//   - Dest is 100x100, white.
// Result:
//   - The image described below will be at an offset of 20, 20
//     The top right half will be alternating between dithered gray and black lines
//     The bottom right half consists of a diagonal white line  on a black background
//     The left half will be completely white
void test_bitblt_palette_1bit__2bit_palette_to_1bit_offset(void) {
  GBitmap *src_bitmap = get_gbitmap_from_pbi("test_bitblt_palette_1bit__2bit_palette_to_1bit.pbi");

  bitblt_bitmap_into_bitmap(&s_dest_bitmap, src_bitmap, GPoint(20, 20), GCompOpAssign, GColorWhite);
  cl_assert(gbitmap_pbi_eq(&s_dest_bitmap,
                           "test_bitblt_palette_1bit__2bit_palette_to_1bit_offset-expect.pbi"));

  gbitmap_destroy(src_bitmap);
}

void test_bitblt_palette_1bit__get_1bit_graphics_grayscale_pattern(void) {
  cl_assert_equal_i(graphics_private_get_1bit_grayscale_pattern(GColorWhite, 0), 0xFFFFFFFF);
  cl_assert_equal_i(graphics_private_get_1bit_grayscale_pattern(GColorWhite, 1), 0xFFFFFFFF);
  cl_assert_equal_i(graphics_private_get_1bit_grayscale_pattern(GColorLightGray, 0), 0x55555555);
  cl_assert_equal_i(graphics_private_get_1bit_grayscale_pattern(GColorLightGray, 1), 0xAAAAAAAA);
  cl_assert_equal_i(graphics_private_get_1bit_grayscale_pattern(GColorDarkGray, 0), 0x55555555);
  cl_assert_equal_i(graphics_private_get_1bit_grayscale_pattern(GColorDarkGray, 1), 0xAAAAAAAA);
  cl_assert_equal_i(graphics_private_get_1bit_grayscale_pattern(GColorBlack, 0), 0x00000000);
  cl_assert_equal_i(graphics_private_get_1bit_grayscale_pattern(GColorBlack, 1), 0x00000000);
}

void test_bitblt_palette_1bit__prv_apply_tint_color(void) {
  GColor color = GColorBlack;
  GColor tinted_color = GColorBlack;
  prv_apply_tint_color(&color, GColorClear);
  cl_assert_equal_i(color.argb, tinted_color.argb);
  tinted_color = GColorRed;
  prv_apply_tint_color(&color, GColorRed);
  cl_assert_equal_i(color.argb, tinted_color.argb);
  tinted_color.a = 2;
  color.a = 2;
  prv_apply_tint_color(&color, GColorRed);
  cl_assert_equal_i(color.argb, tinted_color.argb);
  tinted_color.a = 1;
  color.a = 1;
  prv_apply_tint_color(&color, GColorRed);
  cl_assert_equal_i(color.argb, tinted_color.argb);
}

void test_bitblt_palette_1bit__rust_matches_c_for_boundaries_and_wrap(void) {
  enum {
    DestWidth = 160,
    DestHeight = 12,
    DestRowBytes = DestWidth / 8,
    SrcRowBytes = 5,
  };
  uint8_t c_dest[DestRowBytes * DestHeight];
  uint8_t rust_dest[DestRowBytes * DestHeight];
  uint8_t src_data[SrcRowBytes * 8];
  GColor palette[] = {
      GColorClear,
      GColorBlack,
      (GColor){.argb = 0x6a},
      GColorWhite,
  };
  GBitmap c_bitmap = {
      .addr = c_dest,
      .row_size_bytes = DestRowBytes,
      .info.format = GBitmapFormat1Bit,
      .info.version = GBITMAP_VERSION_CURRENT,
      .bounds = GRect(0, 0, DestWidth, DestHeight),
  };
  GBitmap rust_bitmap = c_bitmap;
  const GRect rects[] = {
      GRect(0, 0, 1, 1),   GRect(1, 1, 31, 2),   GRect(31, 2, 32, 3),
      GRect(32, 1, 33, 4), GRect(33, 0, 113, 7), GRect(127, 3, 17, 5),
  };
  const GPoint offsets[] = {GPoint(0, 0), GPoint(12, 2)};
  const GCompOp modes[] = {
      GCompOpAssign,
      GCompOpSet,
      GCompOpTint,
      GCompOpTintLuminance,
  };

  for (unsigned int i = 0; i < sizeof(src_data); ++i) {
    src_data[i] = (uint8_t)(i * 73 + 19);
  }
  for (unsigned int bpp = 1; bpp <= 2; ++bpp) {
    GBitmap src_bitmap = {
        .addr = src_data,
        .row_size_bytes = SrcRowBytes,
        .info.format = bpp == 1 ? GBitmapFormat1BitPalette : GBitmapFormat2BitPalette,
        .info.version = GBITMAP_VERSION_CURRENT,
        .bounds = GRect(3, 2, 13, 3),
        .palette = palette,
    };
    for (unsigned int r = 0; r < sizeof(rects) / sizeof(rects[0]); ++r) {
      for (unsigned int o = 0; o < sizeof(offsets) / sizeof(offsets[0]); ++o) {
        for (unsigned int m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m) {
          for (unsigned int i = 0; i < sizeof(c_dest); ++i) {
            c_dest[i] = rust_dest[i] = (uint8_t)(i * 29 + r * 11 + 7);
          }
          rust_bitmap.addr = rust_dest;
          bitblt_bitmap_into_bitmap_tiled_palette_to_1bit_c(&c_bitmap, &src_bitmap, rects[r],
                                                            offsets[o], modes[m], GColorRed);
          bitblt_bitmap_into_bitmap_tiled_palette_to_1bit(&rust_bitmap, &src_bitmap, rects[r],
                                                          offsets[o], modes[m], GColorRed);
          cl_assert_equal_i(memcmp(c_dest, rust_dest, sizeof(c_dest)), 0);
        }
      }
    }
  }
}

void test_bitblt_palette_1bit__rust_matches_c_after_clipping(void) {
  uint32_t c_dest[3 * 4];
  uint32_t rust_dest[3 * 4];
  uint8_t src_data[4 * 5];
  GColor palette[] = {GColorClear, GColorBlack, GColorLightGray, GColorWhite};
  GBitmap c_bitmap = {
      .addr = c_dest,
      .row_size_bytes = 12,
      .info.format = GBitmapFormat1Bit,
      .info.version = GBITMAP_VERSION_CURRENT,
      .bounds = GRect(0, 0, 96, 4),
  };
  GBitmap rust_bitmap = c_bitmap;
  GBitmap src_bitmap = {
      .addr = src_data,
      .row_size_bytes = 4,
      .info.format = GBitmapFormat2BitPalette,
      .info.version = GBITMAP_VERSION_CURRENT,
      .bounds = GRect(0, 0, 13, 3),
      .palette = palette,
  };

  for (unsigned int i = 0; i < sizeof(src_data); ++i) {
    src_data[i] = (uint8_t)(i * 41 + 3);
  }
  memset(c_dest, 0xa5, sizeof(c_dest));
  memcpy(rust_dest, c_dest, sizeof(c_dest));
  rust_bitmap.addr = rust_dest;

  GBitmap clipped_source = src_bitmap;
  clipped_source.bounds = GRect(5, 1, 13, 3);
  bitblt_bitmap_into_bitmap_tiled_palette_to_1bit_c(&c_bitmap, &clipped_source, GRect(0, 0, 8, 2),
                                                    GPointZero, GCompOpSet, GColorWhite);
  bitblt_bitmap_into_bitmap(&rust_bitmap, &src_bitmap, GPoint(-5, -1), GCompOpSet, GColorWhite);
  cl_assert_equal_i(memcmp(c_dest, rust_dest, sizeof(c_dest)), 0);
}
