/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EPIC_SCALE_ONE 1024

typedef enum {
  EpicPixelFormat_RGB565,
  EpicPixelFormat_ARGB8565,
  EpicPixelFormat_RGB888,
  EpicPixelFormat_ARGB8888,
  EpicPixelFormat_L8,
  EpicPixelFormat_A8,
  EpicPixelFormat_A4,
  EpicPixelFormat_A2,
  EpicPixelFormat_Mono,
  EpicPixelFormat_YUV422_YUYV,
  EpicPixelFormat_YUV422_UYVY,
  EpicPixelFormat_YUV420_Planar,
  EpicPixelFormat_EZIP,
} EpicPixelFormat;

typedef enum {
  EpicAlphaMode_Normal,
  EpicAlphaMode_Mask,
} EpicAlphaMode;

typedef struct {
  int16_t x;
  int16_t y;
  uint16_t width;
  uint16_t height;
} EpicRect;

typedef struct {
  uint8_t *data;
  //! U and V planes for YUV420 planar input.
  uint8_t *u_data;
  uint8_t *v_data;
  //! Compressed byte count for EZIP input.
  uint32_t data_size;
  EpicPixelFormat format;
  EpicAlphaMode alpha_mode;
  uint16_t width;
  uint16_t height;
  uint16_t stride_pixels;
  uint16_t buffer_height;
  uint16_t data_x;
  uint16_t data_y;
  int16_t x;
  int16_t y;
  uint8_t alpha;
  //! RGB colour used by A2, A4 and A8 layers. The alpha byte is ignored.
  uint32_t color_argb8888;
  const uint32_t *palette;
  uint16_t palette_entries;
  //! Clockwise rotation in tenths of a degree.
  int16_t angle;
  uint16_t pivot_x;
  uint16_t pivot_y;
  uint16_t scale_x;
  uint16_t scale_y;
  bool h_mirror;
  bool v_mirror;
} EpicLayer;

typedef struct {
  uint8_t *data;
  EpicPixelFormat format;
  uint16_t width;
  uint16_t height;
  uint16_t stride_pixels;
  uint16_t buffer_height;
  uint16_t data_x;
  uint16_t data_y;
  int16_t x;
  int16_t y;
} EpicBuffer;

typedef struct {
  uint32_t top_left;
  uint32_t top_right;
  uint32_t bottom_left;
  uint32_t bottom_right;
} EpicGradient;

typedef struct {
  uint32_t fill_cycles;
  uint32_t gradient_cycles;
  uint32_t copy_cycles;
  uint32_t blend_cycles;
  uint32_t rotate_cycles;
  uint32_t scale_cycles;
  uint32_t mirror_cycles;
  uint32_t mask_cycles;
  uint32_t l8_cycles;
  uint32_t mono_cycles;
  uint32_t yuv_cycles;
  bool output_valid;
} EpicBenchmarkResult;

//! Called from the EPIC interrupt after destination cache maintenance completes.
typedef void (*EpicCompleteCallback)(void *context);

bool epic_init(void);
bool epic_layer_set_source_rect(EpicLayer *layer, EpicRect rect, uint16_t buffer_height);
bool epic_buffer_set_destination_rect(EpicBuffer *buffer, EpicRect rect, uint16_t buffer_height);
bool epic_clip_layer(EpicLayer *layer, EpicRect clip);
bool epic_fill(const EpicBuffer *destination, uint32_t argb8888);
bool epic_fill_gradient(const EpicBuffer *destination, const EpicGradient *gradient);
bool epic_copy(const EpicLayer *source, const EpicBuffer *destination);
bool epic_blend(const EpicLayer *layers, size_t layer_count, const EpicBuffer *destination);
bool epic_fill_async(const EpicBuffer *destination, uint32_t argb8888,
                     EpicCompleteCallback callback, void *context);
bool epic_fill_gradient_async(const EpicBuffer *destination, const EpicGradient *gradient,
                              EpicCompleteCallback callback, void *context);
bool epic_copy_async(const EpicLayer *source, const EpicBuffer *destination,
                     EpicCompleteCallback callback, void *context);
bool epic_blend_async(const EpicLayer *layers, size_t layer_count, const EpicBuffer *destination,
                      EpicCompleteCallback callback, void *context);
void epic_irq_handler(void *unused);
void epic_ezip_irq_handler(void *unused);

//! Populate a 256-entry ARGB8888 palette mapping Pebble ARGB2222 pixels to opaque RGB.
void epic_build_gcolor8_palette(uint32_t palette[256]);

//! Run a small destructive self-test and benchmark using private buffers.
bool epic_run_benchmark(EpicBenchmarkResult *result);
