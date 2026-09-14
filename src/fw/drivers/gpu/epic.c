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
static bool s_initialized;

static ALIGN(32) uint16_t s_benchmark_a[EPIC_BENCHMARK_PIXELS];
static ALIGN(32) uint16_t s_benchmark_b[EPIC_BENCHMARK_PIXELS];
static ALIGN(32) uint16_t s_benchmark_output[EPIC_BENCHMARK_PIXELS];
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

static void prv_recover_locked(void) {
  HAL_NVIC_DisableIRQ(EPIC_IRQn);
  HAL_RCC_ResetModule(RCC_MOD_EPIC);
  HAL_NVIC_ClearPendingIRQ(EPIC_IRQn);
  s_initialized = false;
  prv_init_locked();
}

static bool prv_wait_locked(HAL_StatusTypeDef start_status, void *destination,
                            size_t destination_size) {
  if (start_status != HAL_OK) {
    PBL_LOG_ERR("EPIC operation start failed: %d", (int)start_status);
    s_handle.XferCpltCallback = NULL;
    prv_recover_locked();
    soc_sf32lb_sleep_release(SOC_SF32LB_DEEPWFI);
    return false;
  }

  if (pbl_sem_take(&s_complete, PBL_MSEC(EPIC_OPERATION_TIMEOUT_MS)) != 0) {
    PBL_LOG_ERR("EPIC operation timed out: state=%d error=0x%lx", (int)s_handle.State,
                (unsigned long)s_handle.ErrorCode);
    prv_recover_locked();
    soc_sf32lb_sleep_release(SOC_SF32LB_DEEPWFI);
    return false;
  }

  prv_cache_complete_destination(destination, destination_size);
  soc_sf32lb_sleep_release(SOC_SF32LB_DEEPWFI);
  return true;
}

static bool prv_begin_locked(const EpicBuffer *destination, size_t *destination_size) {
  if (!destination || !destination->data || !destination->width || !destination->height ||
      destination->stride_pixels < destination->width ||
      !prv_valid_output_format(destination->format)) {
    return false;
  }
  if (!prv_init_locked()) {
    return false;
  }

  *destination_size =
      prv_buffer_size(destination->format, destination->stride_pixels, destination->height);
  prv_cache_prepare_destination(destination->data, *destination_size);
  pbl_sem_reset(&s_complete);
  s_handle.XferCpltCallback = prv_complete_callback;
  soc_sf32lb_sleep_block(SOC_SF32LB_DEEPWFI);
  return true;
}

static void prv_configure_output(EPIC_LayerConfigTypeDef *output, const EpicBuffer *destination) {
  HAL_EPIC_LayerConfigInit(output);
  output->data = destination->data;
  output->color_mode = prv_hal_format(destination->format);
  output->width = destination->width;
  output->height = destination->height;
  output->total_width = destination->stride_pixels;
  output->x_offset = destination->x;
  output->y_offset = destination->y;
  output->alpha = EPIC_LAYER_OPAQUE;
}

static bool prv_valid_input(const EpicLayer *source) {
  if (!source->data || !source->width || !source->height || source->stride_pixels < source->width ||
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
  input->data = source->data;
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

  prv_cache_flush(source->data,
                  prv_buffer_size(source->format, source->stride_pixels, source->height));
  if (source->palette) {
    prv_cache_flush(source->palette, source->palette_entries * sizeof(*source->palette));
  }
}

bool epic_fill(const EpicBuffer *destination, uint32_t argb8888) {
  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  size_t destination_size;
  if (!prv_begin_locked(destination, &destination_size)) {
    pbl_mutex_unlock(&s_mutex);
    return false;
  }

  EPIC_FillingCfgTypeDef fill;
  HAL_EPIC_FillDataInit(&fill);
  fill.start = destination->data;
  fill.color_mode = prv_hal_format(destination->format);
  fill.width = destination->width;
  fill.height = destination->height;
  fill.total_width = destination->stride_pixels;
  fill.color_r = (argb8888 >> 16) & 0xff;
  fill.color_g = (argb8888 >> 8) & 0xff;
  fill.color_b = argb8888 & 0xff;
  fill.alpha = argb8888 >> 24;

  bool success =
      prv_wait_locked(HAL_EPIC_FillStart_IT(&s_handle, &fill), destination->data, destination_size);
  pbl_mutex_unlock(&s_mutex);
  return success;
}

bool epic_copy(const EpicLayer *source, const EpicBuffer *destination) {
  if (!source || !prv_valid_input(source) || source->alpha_mode == EpicAlphaMode_Mask) {
    return false;
  }
  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  size_t destination_size;
  if (!prv_begin_locked(destination, &destination_size)) {
    pbl_mutex_unlock(&s_mutex);
    return false;
  }

  EPIC_LayerConfigTypeDef input;
  EPIC_LayerConfigTypeDef output;
  prv_configure_input(&input, source);
  prv_configure_output(&output, destination);
  bool success = prv_wait_locked(HAL_EPIC_Copy_IT(&s_handle, (EPIC_BlendingDataType *)&input,
                                                  (EPIC_BlendingDataType *)&output),
                                 destination->data, destination_size);
  pbl_mutex_unlock(&s_mutex);
  return success;
}

bool epic_blend(const EpicLayer *layers, size_t layer_count, const EpicBuffer *destination) {
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
  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  size_t destination_size;
  if (!prv_begin_locked(destination, &destination_size)) {
    pbl_mutex_unlock(&s_mutex);
    return false;
  }

  EPIC_LayerConfigTypeDef inputs[EPIC_MAX_INPUT_LAYERS];
  EPIC_LayerConfigTypeDef output;
  for (size_t i = 0; i < layer_count; ++i) {
    prv_configure_input(&inputs[i], &layers[i]);
  }
  prv_configure_output(&output, destination);
  bool success = prv_wait_locked(HAL_EPIC_BlendStartEx_IT(&s_handle, inputs, layer_count, &output),
                                 destination->data, destination_size);
  pbl_mutex_unlock(&s_mutex);
  return success;
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

static uint32_t prv_measure_copy(const EpicLayer *source, const EpicBuffer *buffer) {
  uint32_t start = DWT->CYCCNT;
  return epic_copy(source, buffer) ? DWT->CYCCNT - start : 0;
}

static uint32_t prv_measure_blend(const EpicLayer *layers, size_t count, const EpicBuffer *buffer) {
  uint32_t start = DWT->CYCCNT;
  return epic_blend(layers, count, buffer) ? DWT->CYCCNT - start : 0;
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
  if (!result->fill_cycles || s_benchmark_a[0] != 0xf800) {
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
  result->copy_cycles = prv_measure_copy(&source, &output);
  if (!result->copy_cycles || s_benchmark_output[0] != 0xf800) {
    return false;
  }

  EpicLayer layers[2] = {source, source};
  layers[1].data = (uint8_t *)s_benchmark_b;
  layers[1].alpha = 128;
  result->blend_cycles = prv_measure_blend(layers, 2, &output);
  if (!result->blend_cycles || s_benchmark_output[0] == 0xf800 || s_benchmark_output[0] == 0x001f) {
    return false;
  }

  source.data = (uint8_t *)s_benchmark_b;
  source.angle = 900;
  source.pivot_x = EPIC_BENCHMARK_SIDE / 2;
  source.pivot_y = EPIC_BENCHMARK_SIDE / 2;
  result->rotate_cycles = prv_measure_blend(&source, 1, &output);
  if (!result->rotate_cycles) {
    return false;
  }

  uint8_t *l8 = (uint8_t *)s_benchmark_a;
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
