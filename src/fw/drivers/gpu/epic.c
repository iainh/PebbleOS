/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/gpu/epic.h>

#include "bf0_hal_epic.h"
#include "pbl/kernel/mutex.h"
#include "pbl/kernel/sem.h"
#include "pbl/mcu/cache.h"
#include "pbl/soc/sf32lb/sleep.h"
#include "pbl/util/attributes.h"
#include <pbl/logging/logging.h>
#include <pbl/util/math.h>

#include <cmsis_core.h>
#include <string.h>

PBL_LOG_MODULE_DEFINE(driver_gpu_epic, CONFIG_DRIVER_GPU_EPIC_LOG_LEVEL);

#define EPIC_OPERATION_TIMEOUT_MS 500
#define EPIC_MAX_INPUT_LAYERS 3
#define EPIC_BENCHMARK_SIDE 32
#define EPIC_BENCHMARK_PIXELS (EPIC_BENCHMARK_SIDE * EPIC_BENCHMARK_SIDE)

static EPIC_HandleTypeDef s_handle;
static PBL_MUTEX_DEFINE(s_mutex);
static PBL_SEM_DEFINE(s_complete, 0, 1);
static PBL_SEM_DEFINE(s_idle, 1, 1);
static bool s_initialized;

typedef struct {
  void *destination;
  size_t destination_size;
  EpicCompleteCallback callback;
  void *context;
  uint32_t generation;
  volatile bool active;
} EpicOperation;

static EpicOperation s_operation;

static ALIGN(32) uint16_t s_benchmark_a[EPIC_BENCHMARK_PIXELS];
static ALIGN(32) uint16_t s_benchmark_b[EPIC_BENCHMARK_PIXELS];
static ALIGN(32) uint16_t s_benchmark_output[EPIC_BENCHMARK_PIXELS];
static ALIGN(32) uint8_t s_benchmark_mask[EPIC_BENCHMARK_PIXELS];
static ALIGN(32) uint32_t s_benchmark_palette[256];

static uint32_t prv_hal_format(EpicPixelFormat format) {
  switch (format) {
    case EpicPixelFormat_RGB565:
      return EPIC_COLOR_RGB565;
    case EpicPixelFormat_ARGB8565:
      return EPIC_COLOR_ARGB8565;
    case EpicPixelFormat_RGB888:
      return EPIC_COLOR_RGB888;
    case EpicPixelFormat_ARGB8888:
      return EPIC_COLOR_ARGB8888;
    case EpicPixelFormat_L8:
      return EPIC_COLOR_L8;
    case EpicPixelFormat_A8:
      return EPIC_COLOR_A8;
    case EpicPixelFormat_A4:
      return EPIC_COLOR_A4;
    case EpicPixelFormat_A2:
      return EPIC_COLOR_A2;
  }
  return UINT32_MAX;
}

static uint8_t prv_bits_per_pixel(EpicPixelFormat format) {
  switch (format) {
    case EpicPixelFormat_RGB565:
      return 16;
    case EpicPixelFormat_ARGB8565:
    case EpicPixelFormat_RGB888:
      return 24;
    case EpicPixelFormat_ARGB8888:
      return 32;
    case EpicPixelFormat_L8:
    case EpicPixelFormat_A8:
      return 8;
    case EpicPixelFormat_A4:
      return 4;
    case EpicPixelFormat_A2:
      return 2;
  }
  return 0;
}

static size_t prv_buffer_size(EpicPixelFormat format, uint16_t stride_pixels, uint16_t height) {
  size_t row_size = ((size_t)prv_bits_per_pixel(format) * stride_pixels + 7) / 8;
  return row_size * height;
}

static bool prv_data_x_is_aligned(EpicPixelFormat format, uint16_t data_x) {
  return ((uint32_t)prv_bits_per_pixel(format) * data_x) % 8 == 0;
}

static uint8_t *prv_region_data(uint8_t *data, EpicPixelFormat format, uint16_t stride_pixels,
                                uint16_t data_x, uint16_t data_y) {
  size_t row_size = prv_buffer_size(format, stride_pixels, 1);
  return data + row_size * data_y + ((size_t)prv_bits_per_pixel(format) * data_x) / 8;
}

static size_t prv_region_size(EpicPixelFormat format, uint16_t stride_pixels, uint16_t width,
                              uint16_t height) {
  if (height == 0) {
    return 0;
  }
  size_t row_size = prv_buffer_size(format, stride_pixels, 1);
  size_t last_row_size = ((size_t)prv_bits_per_pixel(format) * width + 7) / 8;
  return row_size * (height - 1) + last_row_size;
}

static bool prv_valid_region(EpicPixelFormat format, uint16_t stride_pixels, uint16_t buffer_height,
                             uint16_t data_x, uint16_t data_y, uint16_t width, uint16_t height) {
  uint8_t bits_per_pixel = prv_bits_per_pixel(format);
  uint32_t effective_height = buffer_height ? buffer_height : (uint32_t)data_y + height;
  return bits_per_pixel && width && height && stride_pixels >= width &&
         data_x <= stride_pixels - width && data_y <= effective_height &&
         height <= effective_height - data_y && prv_data_x_is_aligned(format, data_x);
}

static bool prv_valid_output_format(EpicPixelFormat format) {
  return format == EpicPixelFormat_RGB565 || format == EpicPixelFormat_ARGB8565 ||
         format == EpicPixelFormat_RGB888 || format == EpicPixelFormat_ARGB8888;
}

static void prv_cache_flush(const void *data, size_t size) {
  uintptr_t address = (uintptr_t)data;
  dcache_align(&address, &size);
  dcache_flush((const void *)address, size);
}

static void prv_cache_prepare_destination(void *data, size_t size) {
  uintptr_t address = (uintptr_t)data;
  dcache_align(&address, &size);
  dcache_flush_invalidate((const void *)address, size);
}

static void prv_cache_complete_destination(void *data, size_t size) {
  uintptr_t address = (uintptr_t)data;
  dcache_align(&address, &size);
  dcache_invalidate((void *)address, size);
}

static void prv_complete_callback(EPIC_HandleTypeDef *handle) {
  prv_cache_complete_destination(s_operation.destination, s_operation.destination_size);
  EpicCompleteCallback callback = s_operation.callback;
  void *context = s_operation.context;
  s_operation.active = false;
  soc_sf32lb_sleep_release(SOC_SF32LB_DEEPWFI);
  pbl_sem_give(&s_idle);
  if (callback) {
    callback(context);
  }
}

static void prv_sync_complete(void *context) {
  pbl_sem_give(&s_complete);
}

static bool prv_init_locked(void) {
  if (s_initialized) {
    return true;
  }

  memset(&s_handle, 0, sizeof(s_handle));
  s_handle.Instance = hwp_epic;
  if (HAL_EPIC_Init(&s_handle) != HAL_OK) {
    PBL_LOG_ERR("EPIC initialization failed");
    return false;
  }

  HAL_NVIC_SetPriority(EPIC_IRQn, 5, 0);
  HAL_NVIC_ClearPendingIRQ(EPIC_IRQn);
  HAL_NVIC_EnableIRQ(EPIC_IRQn);
  s_initialized = true;
  return true;
}

bool epic_init(void) {
  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  bool initialized = prv_init_locked();
  pbl_mutex_unlock(&s_mutex);
  return initialized;
}

bool epic_layer_set_source_rect(EpicLayer *layer, EpicRect rect, uint16_t buffer_height) {
  if (!layer || rect.x < 0 || rect.y < 0 ||
      !prv_valid_region(layer->format, layer->stride_pixels, buffer_height, rect.x, rect.y,
                        rect.width, rect.height)) {
    return false;
  }
  layer->buffer_height = buffer_height;
  layer->data_x = rect.x;
  layer->data_y = rect.y;
  layer->width = rect.width;
  layer->height = rect.height;
  return true;
}

bool epic_buffer_set_destination_rect(EpicBuffer *buffer, EpicRect rect, uint16_t buffer_height) {
  if (!buffer || rect.x < 0 || rect.y < 0 ||
      !prv_valid_region(buffer->format, buffer->stride_pixels, buffer_height, rect.x, rect.y,
                        rect.width, rect.height)) {
    return false;
  }
  buffer->buffer_height = buffer_height;
  buffer->data_x = rect.x;
  buffer->data_y = rect.y;
  buffer->width = rect.width;
  buffer->height = rect.height;
  return true;
}

bool epic_clip_layer(EpicLayer *layer, EpicRect clip) {
  if (!layer || !clip.width || !clip.height || layer->angle || layer->h_mirror || layer->v_mirror ||
      (layer->scale_x && layer->scale_x != EPIC_SCALE_ONE) ||
      (layer->scale_y && layer->scale_y != EPIC_SCALE_ONE) ||
      !prv_valid_region(layer->format, layer->stride_pixels, layer->buffer_height, layer->data_x,
                        layer->data_y, layer->width, layer->height)) {
    return false;
  }

  int32_t x0 = MAX(layer->x, clip.x);
  int32_t y0 = MAX(layer->y, clip.y);
  int32_t x1 = MIN((int32_t)layer->x + layer->width, (int32_t)clip.x + clip.width);
  int32_t y1 = MIN((int32_t)layer->y + layer->height, (int32_t)clip.y + clip.height);
  if (x0 >= x1 || y0 >= y1) {
    return false;
  }

  uint32_t data_x = (uint32_t)layer->data_x + x0 - layer->x;
  if (!prv_data_x_is_aligned(layer->format, data_x)) {
    return false;
  }
  layer->data_x = data_x;
  layer->data_y += y0 - layer->y;
  layer->x = x0;
  layer->y = y0;
  layer->width = x1 - x0;
  layer->height = y1 - y0;
  return true;
}

static void prv_recover_locked(void) {
  HAL_NVIC_DisableIRQ(EPIC_IRQn);
  HAL_RCC_ResetModule(RCC_MOD_EPIC);
  HAL_NVIC_ClearPendingIRQ(EPIC_IRQn);
  s_initialized = false;
  prv_init_locked();
}

static bool prv_start_locked(HAL_StatusTypeDef start_status) {
  if (start_status != HAL_OK) {
    PBL_LOG_ERR("EPIC operation start failed: %d", (int)start_status);
    s_handle.XferCpltCallback = NULL;
    s_operation.active = false;
    prv_recover_locked();
    soc_sf32lb_sleep_release(SOC_SF32LB_DEEPWFI);
    pbl_sem_give(&s_idle);
    pbl_mutex_unlock(&s_mutex);
    return false;
  }

  pbl_mutex_unlock(&s_mutex);
  return true;
}

static bool prv_wait(uint32_t generation) {
  if (pbl_sem_take(&s_complete, PBL_MSEC(EPIC_OPERATION_TIMEOUT_MS)) == 0) {
    return true;
  }

  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  HAL_NVIC_DisableIRQ(EPIC_IRQn);
  bool timed_out = s_operation.active && s_operation.generation == generation;
  if (timed_out) {
    PBL_LOG_ERR("EPIC operation timed out: state=%d error=0x%lx", (int)s_handle.State,
                (unsigned long)s_handle.ErrorCode);
    s_operation.active = false;
    prv_recover_locked();
    soc_sf32lb_sleep_release(SOC_SF32LB_DEEPWFI);
    pbl_sem_give(&s_idle);
  } else {
    HAL_NVIC_EnableIRQ(EPIC_IRQn);
  }
  pbl_mutex_unlock(&s_mutex);

  return !timed_out && pbl_sem_take(&s_complete, PBL_NO_WAIT) == 0;
}

static bool prv_begin(const EpicBuffer *destination, pbl_timeout_t timeout,
                      EpicCompleteCallback callback, void *context, uint32_t *generation) {
  if (!destination || !destination->data || !destination->width || !destination->height ||
      !prv_valid_region(destination->format, destination->stride_pixels, destination->buffer_height,
                        destination->data_x, destination->data_y, destination->width,
                        destination->height) ||
      !prv_valid_output_format(destination->format)) {
    return false;
  }
  if (pbl_sem_take(&s_idle, timeout) != 0) {
    return false;
  }
  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  if (!prv_init_locked()) {
    pbl_mutex_unlock(&s_mutex);
    pbl_sem_give(&s_idle);
    return false;
  }
  if (callback == prv_sync_complete) {
    pbl_sem_reset(&s_complete);
  }

  s_operation.destination =
      prv_region_data(destination->data, destination->format, destination->stride_pixels,
                      destination->data_x, destination->data_y);
  s_operation.destination_size = prv_region_size(destination->format, destination->stride_pixels,
                                                 destination->width, destination->height);
  s_operation.callback = callback;
  s_operation.context = context;
  s_operation.generation++;
  s_operation.active = true;
  if (generation) {
    *generation = s_operation.generation;
  }
  prv_cache_prepare_destination(destination->data, s_operation.destination_size);
  s_handle.XferCpltCallback = prv_complete_callback;
  soc_sf32lb_sleep_block(SOC_SF32LB_DEEPWFI);
  return true;
}

static void prv_configure_output(EPIC_LayerConfigTypeDef *output, const EpicBuffer *destination) {
  HAL_EPIC_LayerConfigInit(output);
  output->data = prv_region_data(destination->data, destination->format, destination->stride_pixels,
                                 destination->data_x, destination->data_y);
  output->color_mode = prv_hal_format(destination->format);
  output->width = destination->width;
  output->height = destination->height;
  output->total_width = destination->stride_pixels;
  output->x_offset = destination->x;
  output->y_offset = destination->y;
  output->alpha = EPIC_LAYER_OPAQUE;
}

static bool prv_valid_input(const EpicLayer *source) {
  if (!source->data ||
      !prv_valid_region(source->format, source->stride_pixels, source->buffer_height,
                        source->data_x, source->data_y, source->width, source->height) ||
      prv_hal_format(source->format) == UINT32_MAX || source->alpha_mode > EpicAlphaMode_Mask) {
    return false;
  }
  if (source->format == EpicPixelFormat_L8 &&
      (!source->palette || !source->palette_entries || source->palette_entries > 256)) {
    return false;
  }
  if (source->alpha_mode == EpicAlphaMode_Mask && source->format != EpicPixelFormat_A4 &&
      source->format != EpicPixelFormat_A8) {
    return false;
  }
  return true;
}

static void prv_configure_input(EPIC_LayerConfigTypeDef *input, const EpicLayer *source) {
  HAL_EPIC_LayerConfigInit(input);
  input->data = prv_region_data(source->data, source->format, source->stride_pixels, source->data_x,
                                source->data_y);
  input->color_mode = prv_hal_format(source->format);
  input->width = source->width;
  input->height = source->height;
  input->total_width = source->stride_pixels;
  input->x_offset = source->x;
  input->y_offset = source->y;
  input->alpha = source->alpha;
  input->ax_mode =
      source->alpha_mode == EpicAlphaMode_Mask ? ALPHA_BLEND_MASK : ALPHA_BLEND_RGBCOLOR;
  if (source->format == EpicPixelFormat_A2 || source->format == EpicPixelFormat_A4 ||
      source->format == EpicPixelFormat_A8) {
    input->color_en = true;
    input->color_r = (source->color_argb8888 >> 16) & 0xff;
    input->color_g = (source->color_argb8888 >> 8) & 0xff;
    input->color_b = source->color_argb8888 & 0xff;
  }
  input->lookup_table = (uint8_t *)source->palette;
  input->lookup_table_size = source->palette_entries;
  input->transform_cfg.angle = source->angle;
  input->transform_cfg.pivot_x = source->pivot_x;
  input->transform_cfg.pivot_y = source->pivot_y;
  input->transform_cfg.scale_x = source->scale_x ? source->scale_x : EPIC_INPUT_SCALE_NONE;
  input->transform_cfg.scale_y = source->scale_y ? source->scale_y : EPIC_INPUT_SCALE_NONE;
  input->transform_cfg.h_mirror = source->h_mirror;
  input->transform_cfg.v_mirror = source->v_mirror;

  prv_cache_flush(input->data, prv_region_size(source->format, source->stride_pixels, source->width,
                                               source->height));
  if (source->palette) {
    prv_cache_flush(source->palette, source->palette_entries * sizeof(*source->palette));
  }
}

static bool prv_fill(const EpicBuffer *destination, uint32_t argb8888, pbl_timeout_t timeout,
                     EpicCompleteCallback callback, void *context, uint32_t *generation) {
  if (!prv_begin(destination, timeout, callback, context, generation)) {
    return false;
  }

  EPIC_FillingCfgTypeDef fill;
  HAL_EPIC_FillDataInit(&fill);
  fill.start = prv_region_data(destination->data, destination->format, destination->stride_pixels,
                               destination->data_x, destination->data_y);
  fill.color_mode = prv_hal_format(destination->format);
  fill.width = destination->width;
  fill.height = destination->height;
  fill.total_width = destination->stride_pixels;
  fill.color_r = (argb8888 >> 16) & 0xff;
  fill.color_g = (argb8888 >> 8) & 0xff;
  fill.color_b = argb8888 & 0xff;
  fill.alpha = argb8888 >> 24;

  return prv_start_locked(HAL_EPIC_FillStart_IT(&s_handle, &fill));
}

static void prv_set_hal_color(EPIC_ColorDef *destination, uint32_t argb8888) {
  destination->ch.color_r = (argb8888 >> 16) & 0xff;
  destination->ch.color_g = (argb8888 >> 8) & 0xff;
  destination->ch.color_b = argb8888 & 0xff;
  destination->ch.alpha = argb8888 >> 24;
}

static bool prv_fill_gradient(const EpicBuffer *destination, const EpicGradient *gradient,
                              pbl_timeout_t timeout, EpicCompleteCallback callback, void *context,
                              uint32_t *generation) {
  if (!gradient || !prv_begin(destination, timeout, callback, context, generation)) {
    return false;
  }

  EPIC_GradCfgTypeDef fill;
  HAL_EPIC_FillGradDataInit(&fill);
  fill.start = prv_region_data(destination->data, destination->format, destination->stride_pixels,
                               destination->data_x, destination->data_y);
  fill.color_mode = prv_hal_format(destination->format);
  fill.width = destination->width;
  fill.height = destination->height;
  fill.total_width = destination->stride_pixels;
  prv_set_hal_color(&fill.color[0][0], gradient->top_left);
  prv_set_hal_color(&fill.color[0][1], gradient->top_right);
  prv_set_hal_color(&fill.color[1][0], gradient->bottom_left);
  prv_set_hal_color(&fill.color[1][1], gradient->bottom_right);

  return prv_start_locked(HAL_EPIC_FillGrad_IT(&s_handle, &fill));
}

static bool prv_copy(const EpicLayer *source, const EpicBuffer *destination, pbl_timeout_t timeout,
                     EpicCompleteCallback callback, void *context, uint32_t *generation) {
  if (!source || !prv_valid_input(source) || source->alpha_mode == EpicAlphaMode_Mask) {
    return false;
  }
  if (!prv_begin(destination, timeout, callback, context, generation)) {
    return false;
  }

  EPIC_LayerConfigTypeDef input;
  EPIC_LayerConfigTypeDef output;
  prv_configure_input(&input, source);
  prv_configure_output(&output, destination);
  return prv_start_locked(HAL_EPIC_Copy_IT(&s_handle, (EPIC_BlendingDataType *)&input,
                                           (EPIC_BlendingDataType *)&output));
}

static bool prv_blend(const EpicLayer *layers, size_t layer_count, const EpicBuffer *destination,
                      pbl_timeout_t timeout, EpicCompleteCallback callback, void *context,
                      uint32_t *generation) {
  if (!layers || !layer_count || layer_count > EPIC_MAX_INPUT_LAYERS) {
    return false;
  }
  bool has_mask = false;
  for (size_t i = 0; i < layer_count; ++i) {
    if (!prv_valid_input(&layers[i])) {
      return false;
    }
    if (layers[i].alpha_mode == EpicAlphaMode_Mask) {
      if (i == 0 || has_mask) {
        return false;
      }
      has_mask = true;
    }
  }
  if (!prv_begin(destination, timeout, callback, context, generation)) {
    return false;
  }

  EPIC_LayerConfigTypeDef inputs[EPIC_MAX_INPUT_LAYERS];
  EPIC_LayerConfigTypeDef output;
  for (size_t i = 0; i < layer_count; ++i) {
    prv_configure_input(&inputs[i], &layers[i]);
  }
  prv_configure_output(&output, destination);
  return prv_start_locked(HAL_EPIC_BlendStartEx_IT(&s_handle, inputs, layer_count, &output));
}

bool epic_fill(const EpicBuffer *destination, uint32_t argb8888) {
  uint32_t generation;
  if (!prv_fill(destination, argb8888, PBL_FOREVER, prv_sync_complete, NULL, &generation)) {
    return false;
  }
  return prv_wait(generation);
}

bool epic_fill_gradient(const EpicBuffer *destination, const EpicGradient *gradient) {
  uint32_t generation;
  if (!prv_fill_gradient(destination, gradient, PBL_FOREVER, prv_sync_complete, NULL,
                         &generation)) {
    return false;
  }
  return prv_wait(generation);
}

bool epic_copy(const EpicLayer *source, const EpicBuffer *destination) {
  uint32_t generation;
  if (!prv_copy(source, destination, PBL_FOREVER, prv_sync_complete, NULL, &generation)) {
    return false;
  }
  return prv_wait(generation);
}

bool epic_blend(const EpicLayer *layers, size_t layer_count, const EpicBuffer *destination) {
  uint32_t generation;
  if (!prv_blend(layers, layer_count, destination, PBL_FOREVER, prv_sync_complete, NULL,
                 &generation)) {
    return false;
  }
  return prv_wait(generation);
}

bool epic_fill_async(const EpicBuffer *destination, uint32_t argb8888,
                     EpicCompleteCallback callback, void *context) {
  return prv_fill(destination, argb8888, PBL_FOREVER, callback, context, NULL);
}

bool epic_fill_gradient_async(const EpicBuffer *destination, const EpicGradient *gradient,
                              EpicCompleteCallback callback, void *context) {
  return prv_fill_gradient(destination, gradient, PBL_FOREVER, callback, context, NULL);
}

bool epic_copy_async(const EpicLayer *source, const EpicBuffer *destination,
                     EpicCompleteCallback callback, void *context) {
  return prv_copy(source, destination, PBL_FOREVER, callback, context, NULL);
}

bool epic_blend_async(const EpicLayer *layers, size_t layer_count, const EpicBuffer *destination,
                      EpicCompleteCallback callback, void *context) {
  return prv_blend(layers, layer_count, destination, PBL_FOREVER, callback, context, NULL);
}

void epic_irq_handler(void *unused) {
  HAL_EPIC_IRQHandler(&s_handle);
}

void epic_build_gcolor8_palette(uint32_t palette[256]) {
  for (uint32_t value = 0; value < 256; ++value) {
    uint32_t red = ((value >> 4) & 3) * 85;
    uint32_t green = ((value >> 2) & 3) * 85;
    uint32_t blue = (value & 3) * 85;
    palette[value] = 0xff000000 | (red << 16) | (green << 8) | blue;
  }
}

static uint32_t prv_measure_fill(const EpicBuffer *buffer, uint32_t color) {
  uint32_t start = DWT->CYCCNT;
  return epic_fill(buffer, color) ? DWT->CYCCNT - start : 0;
}

static uint32_t prv_measure_gradient(const EpicBuffer *buffer, const EpicGradient *gradient) {
  uint32_t start = DWT->CYCCNT;
  return epic_fill_gradient(buffer, gradient) ? DWT->CYCCNT - start : 0;
}

static uint32_t prv_measure_copy(const EpicLayer *source, const EpicBuffer *buffer) {
  uint32_t start = DWT->CYCCNT;
  return epic_copy(source, buffer) ? DWT->CYCCNT - start : 0;
}

static uint32_t prv_measure_blend(const EpicLayer *layers, size_t count, const EpicBuffer *buffer) {
  uint32_t start = DWT->CYCCNT;
  return epic_blend(layers, count, buffer) ? DWT->CYCCNT - start : 0;
}

static bool prv_all_pixels_equal(const uint16_t *buffer, uint16_t value) {
  for (size_t i = 0; i < EPIC_BENCHMARK_PIXELS; ++i) {
    if (buffer[i] != value) {
      return false;
    }
  }
  return true;
}

static void prv_build_asymmetric_pattern(uint16_t *buffer) {
  for (uint16_t y = 0; y < EPIC_BENCHMARK_SIDE; ++y) {
    for (uint16_t x = 0; x < EPIC_BENCHMARK_SIDE; ++x) {
      buffer[y * EPIC_BENCHMARK_SIDE + x] = ((x + 1) << 11) | ((y + 1) << 5) | ((x + y) & 31);
    }
  }
}

static bool prv_is_reversed_pattern(const uint16_t *output, const uint16_t *input) {
  for (uint16_t y = 0; y < EPIC_BENCHMARK_SIDE; ++y) {
    for (uint16_t x = 0; x < EPIC_BENCHMARK_SIDE; ++x) {
      size_t input_index =
          (EPIC_BENCHMARK_SIDE - y - 1) * EPIC_BENCHMARK_SIDE + EPIC_BENCHMARK_SIDE - x - 1;
      if (output[y * EPIC_BENCHMARK_SIDE + x] != input[input_index]) {
        return false;
      }
    }
  }
  return true;
}

static size_t prv_count_nonzero_pixels(const uint16_t *buffer) {
  size_t count = 0;
  for (size_t i = 0; i < EPIC_BENCHMARK_PIXELS; ++i) {
    count += buffer[i] != 0;
  }
  return count;
}

bool epic_run_benchmark(EpicBenchmarkResult *result) {
  if (!result) {
    return false;
  }
  memset(result, 0, sizeof(*result));
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  EpicBuffer a = {
      .data = (uint8_t *)s_benchmark_a,
      .format = EpicPixelFormat_RGB565,
      .width = EPIC_BENCHMARK_SIDE,
      .height = EPIC_BENCHMARK_SIDE,
      .stride_pixels = EPIC_BENCHMARK_SIDE,
  };
  EpicBuffer b = a;
  b.data = (uint8_t *)s_benchmark_b;
  EpicBuffer output = a;
  output.data = (uint8_t *)s_benchmark_output;

  result->fill_cycles = prv_measure_fill(&a, 0xffff0000);
  if (!result->fill_cycles || !prv_all_pixels_equal(s_benchmark_a, 0xf800)) {
    return false;
  }
  EpicGradient gradient = {
      .top_left = 0xffff0000,
      .top_right = 0xff00ff00,
      .bottom_left = 0xff0000ff,
      .bottom_right = 0xffffffff,
  };
  result->gradient_cycles = prv_measure_gradient(&output, &gradient);
  if (!result->gradient_cycles || s_benchmark_output[0] != 0xf800 ||
      s_benchmark_output[EPIC_BENCHMARK_SIDE - 1] != 0x07e0 ||
      s_benchmark_output[EPIC_BENCHMARK_PIXELS - EPIC_BENCHMARK_SIDE] != 0x001f ||
      s_benchmark_output[EPIC_BENCHMARK_PIXELS - 1] != 0xffff) {
    return false;
  }
  if (!epic_fill(&b, 0xff0000ff)) {
    return false;
  }

  EpicLayer source = {
      .data = (uint8_t *)s_benchmark_a,
      .format = EpicPixelFormat_RGB565,
      .alpha_mode = EpicAlphaMode_Normal,
      .width = EPIC_BENCHMARK_SIDE,
      .height = EPIC_BENCHMARK_SIDE,
      .stride_pixels = EPIC_BENCHMARK_SIDE,
      .alpha = 255,
  };
  EpicLayer clipped = source;
  clipped.x = -2;
  clipped.y = -3;
  if (!epic_layer_set_source_rect(&clipped, (EpicRect){4, 6, 10, 12}, EPIC_BENCHMARK_SIDE) ||
      !epic_clip_layer(&clipped, (EpicRect){0, 0, 8, 8}) || clipped.data_x != 6 ||
      clipped.data_y != 9 || clipped.x != 0 || clipped.y != 0 || clipped.width != 8 ||
      clipped.height != 8) {
    return false;
  }
  EpicBuffer subregion = output;
  if (!epic_buffer_set_destination_rect(&subregion, (EpicRect){3, 4, 8, 9}, EPIC_BENCHMARK_SIDE) ||
      subregion.data_x != 3 || subregion.data_y != 4 || subregion.width != 8 ||
      subregion.height != 9) {
    return false;
  }
  EpicLayer source_subregion = source;
  if (!epic_layer_set_source_rect(&source_subregion, (EpicRect){2, 3, 8, 9}, EPIC_BENCHMARK_SIDE) ||
      !epic_fill(&output, 0xff000000) || !epic_copy(&source_subregion, &subregion) ||
      s_benchmark_output[4 * EPIC_BENCHMARK_SIDE + 3] != 0xf800 ||
      s_benchmark_output[3 * EPIC_BENCHMARK_SIDE + 3] != 0x0000 ||
      s_benchmark_output[4 * EPIC_BENCHMARK_SIDE + 2] != 0x0000 ||
      s_benchmark_output[12 * EPIC_BENCHMARK_SIDE + 10] != 0xf800 ||
      s_benchmark_output[13 * EPIC_BENCHMARK_SIDE + 10] != 0x0000 ||
      s_benchmark_output[12 * EPIC_BENCHMARK_SIDE + 11] != 0x0000) {
    return false;
  }
  result->copy_cycles = prv_measure_copy(&source, &output);
  if (!result->copy_cycles || !prv_all_pixels_equal(s_benchmark_output, 0xf800)) {
    return false;
  }

  EpicLayer layers[2] = {source, source};
  layers[1].data = (uint8_t *)s_benchmark_b;
  layers[1].alpha = 128;
  result->blend_cycles = prv_measure_blend(layers, 2, &output);
  if (!result->blend_cycles || s_benchmark_output[0] == 0xf800 || s_benchmark_output[0] == 0x001f) {
    return false;
  }

  prv_build_asymmetric_pattern(s_benchmark_a);
  source.data = (uint8_t *)s_benchmark_a;
  source.angle = 1800;
  source.pivot_x = EPIC_BENCHMARK_SIDE / 2;
  source.pivot_y = EPIC_BENCHMARK_SIDE / 2;
  result->rotate_cycles = prv_measure_blend(&source, 1, &output);
  if (!result->rotate_cycles || !prv_is_reversed_pattern(s_benchmark_output, s_benchmark_a)) {
    return false;
  }

  source.angle = 0;
  source.pivot_x = 0;
  source.pivot_y = 0;
  source.h_mirror = true;
  source.v_mirror = true;
  result->mirror_cycles = prv_measure_blend(&source, 1, &output);
  if (!result->mirror_cycles || !prv_is_reversed_pattern(s_benchmark_output, s_benchmark_a)) {
    return false;
  }

  source.h_mirror = false;
  source.v_mirror = false;
  source.scale_x = EPIC_SCALE_ONE * 2;
  source.scale_y = EPIC_SCALE_ONE * 2;
  if (!epic_fill(&output, 0xff000000)) {
    return false;
  }
  result->scale_cycles = prv_measure_blend(&source, 1, &output);
  size_t scaled_pixels = prv_count_nonzero_pixels(s_benchmark_output);
  if (!result->scale_cycles || scaled_pixels < 128 || scaled_pixels > 512) {
    return false;
  }

  if (!epic_fill(&a, 0xffff0000) || !epic_fill(&b, 0xff0000ff)) {
    return false;
  }
  for (uint16_t y = 0; y < EPIC_BENCHMARK_SIDE; ++y) {
    for (uint16_t x = 0; x < EPIC_BENCHMARK_SIDE; ++x) {
      s_benchmark_mask[y * EPIC_BENCHMARK_SIDE + x] = x < EPIC_BENCHMARK_SIDE / 2 ? 0 : 255;
    }
  }
  EpicLayer mask_layers[3] = {
      {
          .data = (uint8_t *)s_benchmark_a,
          .format = EpicPixelFormat_RGB565,
          .width = EPIC_BENCHMARK_SIDE,
          .height = EPIC_BENCHMARK_SIDE,
          .stride_pixels = EPIC_BENCHMARK_SIDE,
          .alpha = 255,
      },
      {
          .data = (uint8_t *)s_benchmark_b,
          .format = EpicPixelFormat_RGB565,
          .width = EPIC_BENCHMARK_SIDE,
          .height = EPIC_BENCHMARK_SIDE,
          .stride_pixels = EPIC_BENCHMARK_SIDE,
          .alpha = 255,
      },
      {
          .data = s_benchmark_mask,
          .format = EpicPixelFormat_A8,
          .alpha_mode = EpicAlphaMode_Mask,
          .width = EPIC_BENCHMARK_SIDE,
          .height = EPIC_BENCHMARK_SIDE,
          .stride_pixels = EPIC_BENCHMARK_SIDE,
          .alpha = 255,
      },
  };
  result->mask_cycles = prv_measure_blend(mask_layers, 3, &output);
  if (!result->mask_cycles || s_benchmark_output[0] != 0xf800 ||
      s_benchmark_output[EPIC_BENCHMARK_SIDE - 1] != 0x001f) {
    return false;
  }

  uint8_t *l8 = s_benchmark_mask;
  memset(l8, 0xc0, EPIC_BENCHMARK_PIXELS);
  l8[0] = 0xff;
  epic_build_gcolor8_palette(s_benchmark_palette);
  source = (EpicLayer){
      .data = l8,
      .format = EpicPixelFormat_L8,
      .alpha_mode = EpicAlphaMode_Normal,
      .width = EPIC_BENCHMARK_SIDE,
      .height = EPIC_BENCHMARK_SIDE,
      .stride_pixels = EPIC_BENCHMARK_SIDE,
      .alpha = 255,
      .palette = s_benchmark_palette,
      .palette_entries = 256,
  };
  result->l8_cycles = prv_measure_blend(&source, 1, &output);
  result->output_valid =
      result->l8_cycles && s_benchmark_output[0] == 0xffff && s_benchmark_output[1] == 0x0000;
  return result->output_valid;
}
