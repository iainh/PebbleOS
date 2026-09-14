/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "board/board.h"
#include "kernel/kernel_heap.h"
#include "pbl/drivers/mic/nrf5/pdm_definitions.h"
#include "pbl/services/system_task.h"
#include "pbl/util/circular_buffer.h"

#include "clar.h"

#include <stdint.h>
#include <string.h>

#include "stubs_logging.h"
#include "stubs_passert.h"

MicDevice *const MIC = NULL;

void pbl_mutex_init(struct pbl_mutex *mutex) {}

int pbl_mutex_lock_lr(struct pbl_mutex *mutex, pbl_timeout_t timeout, uintptr_t lr) {
  return 0;
}

void pbl_mutex_unlock(struct pbl_mutex *mutex) {}

void *kernel_malloc(size_t bytes) {
  return NULL;
}

void kernel_free(void *ptr) {}

Heap *kernel_heap_get(void) {
  return NULL;
}

void clocksource_hfxo_request(void) {}

void clocksource_hfxo_release(void) {}

void psleep(int millis) {}

void prompt_send_response(const char *response) {}

bool system_task_add_callback_from_isr_droppable(SystemTaskEventCallback callback, void *data,
                                                 bool *should_context_switch) {
  return false;
}

void system_task_watchdog_feed(void) {}

bool system_task_add_callback(SystemTaskEventCallback callback, void *data) {
  return false;
}

nrfx_err_t nrfx_pdm_init(const nrfx_pdm_t *instance, const nrfx_pdm_config_t *config,
                         nrfx_pdm_event_handler_t event_handler) {
  return NRFX_SUCCESS;
}

nrfx_err_t nrfx_pdm_start(const nrfx_pdm_t *instance) {
  return NRFX_SUCCESS;
}

nrfx_err_t nrfx_pdm_stop(const nrfx_pdm_t *instance) {
  return NRFX_SUCCESS;
}

nrfx_err_t nrfx_pdm_buffer_set(const nrfx_pdm_t *instance, int16_t *buffer,
                               uint16_t buffer_length) {
  return NRFX_SUCCESS;
}

uint16_t prv_write_pdm_samples(CircularBuffer *buffer, const int16_t *samples,
                               uint16_t sample_count);

void test_nrf5_pdm__initialize(void) {}

void test_nrf5_pdm__cleanup(void) {}

void test_nrf5_pdm__writes_partial_aligned_samples(void) {
  CircularBuffer buffer;
  uint8_t storage[9];
  circular_buffer_init(&buffer, storage, sizeof(storage));
  cl_assert(circular_buffer_write(&buffer, "ABCD", 4));

  const int16_t samples[] = {0x0102, 0x0304, 0x0506};
  cl_assert_equal_i(prv_write_pdm_samples(&buffer, samples, 3), 2);
  cl_assert_equal_i(circular_buffer_get_read_space_remaining(&buffer), 8);
  cl_assert_equal_i(circular_buffer_get_write_space_remaining(&buffer), 1);

  uint8_t output[sizeof(storage)] = {0};
  cl_assert_equal_i(circular_buffer_copy(&buffer, output, sizeof(output)), 8);
  cl_assert_equal_m(output, "ABCD\x02\x01\x04\x03", 8);
}

void test_nrf5_pdm__wraps_partial_write_and_reports_dropped_sample(void) {
  CircularBuffer buffer;
  uint8_t storage[10];
  circular_buffer_init(&buffer, storage, sizeof(storage));
  cl_assert(circular_buffer_write(&buffer, "ABCDEFGH", 8));
  cl_assert(circular_buffer_consume(&buffer, 4));

  const int16_t samples[] = {0x0102, 0x0304, 0x0506, 0x0708};
  const uint16_t samples_written = prv_write_pdm_samples(&buffer, samples, 4);
  cl_assert_equal_i(samples_written, 3);
  cl_assert_equal_i(4 - samples_written, 1);

  uint8_t output[sizeof(storage)] = {0};
  cl_assert_equal_i(circular_buffer_copy(&buffer, output, sizeof(output)), sizeof(output));
  cl_assert_equal_m(output, "EFGH\x02\x01\x04\x03\x06\x05", sizeof(output));
}
