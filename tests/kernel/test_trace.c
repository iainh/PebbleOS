/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "clar.h"

#include "pbl/kernel/trace.h"
#include "pbl/kernel/thread.h"

static struct pbl_trace_record s_records[CONFIG_KERNEL_TRACE_RECORDS];
static unsigned s_irq_depth;
static bool s_fault_during_write;
static struct pbl_thread s_thread = {.id = 73};

void pbl_irq_lock(void) {
  s_irq_depth++;
}
void pbl_irq_unlock(void) {
  cl_assert(s_irq_depth-- > 0);
}
struct pbl_thread *pbl_thread_current(void) {
  return &s_thread;
}
pbl_tick_t pbl_uptime_ticks(void) {
  if (s_fault_during_write) {
    pbl_trace_freeze();
  }
  return 321;
}

void test_trace__initialize(void) {
  s_fault_during_write = false;
  s_irq_depth = 0;
  pbl_trace_reset();
  pbl_trace_set_mask(PBL_TRACE_CAT_ALL);
  pbl_trace_start();
}

void test_trace__cleanup(void) {
  cl_assert_equal_i(s_irq_depth, 0);
}

void test_trace__wrap_is_returned_oldest_first(void) {
  for (uintptr_t i = 0; i < CONFIG_KERNEL_TRACE_RECORDS + 2; ++i) {
    pbl_trace_record(PBL_TRACE_SCHED_WAKE, i, i + 10);
  }

  struct pbl_trace_snapshot_info info;
  size_t count = pbl_trace_snapshot(s_records, CONFIG_KERNEL_TRACE_RECORDS, &info);
  cl_assert_equal_i(count, CONFIG_KERNEL_TRACE_RECORDS);
  cl_assert_equal_i(info.available, CONFIG_KERNEL_TRACE_RECORDS);
  cl_assert_equal_i(info.overwritten, 2);
  for (size_t i = 0; i < count; ++i) {
    cl_assert_equal_i(s_records[i].arg0, i + 2);
    cl_assert_equal_i(s_records[i].arg1, i + 12);
    cl_assert_equal_i(s_records[i].timestamp, 321);
    cl_assert_equal_i(s_records[i].thread_id, 73);
    if (i) {
      cl_assert_equal_i(s_records[i].sequence, s_records[i - 1].sequence + 1);
    }
  }
}

void test_trace__filter_stop_and_freeze(void) {
  pbl_trace_set_mask(PBL_TRACE_CAT_MUTEX);
  pbl_trace_record(PBL_TRACE_SCHED_BLOCK, 1, 0);
  pbl_trace_record(PBL_TRACE_MUTEX_CONTENTION, 2, 0);
  pbl_trace_stop();
  pbl_trace_record(PBL_TRACE_MUTEX_CONTENTION, 3, 0);
  pbl_trace_start();
  pbl_trace_record(PBL_TRACE_MUTEX_CONTENTION, 4, 0);
  pbl_trace_freeze();
  pbl_trace_record(PBL_TRACE_MUTEX_CONTENTION, 5, 0);
  pbl_trace_start();
  pbl_trace_record(PBL_TRACE_MUTEX_CONTENTION, 6, 0);

  cl_assert(pbl_trace_is_frozen());
  cl_assert_equal_i(pbl_trace_snapshot(s_records, CONFIG_KERNEL_TRACE_RECORDS, NULL), 2);
  cl_assert_equal_i(s_records[0].arg0, 2);
  cl_assert_equal_i(s_records[1].arg0, 4);
}

void test_trace__snapshot_capacity_keeps_newest(void) {
  for (uintptr_t i = 0; i < 4; ++i) {
    pbl_trace_record(PBL_TRACE_QUEUE_FULL, i, 0);
  }

  struct pbl_trace_snapshot_info info;
  cl_assert_equal_i(pbl_trace_snapshot(s_records, 2, &info), 2);
  cl_assert_equal_i(info.available, 4);
  cl_assert_equal_i(s_records[0].arg0, 2);
  cl_assert_equal_i(s_records[1].arg0, 3);
}

void test_trace__reset_clears_and_unfreezes(void) {
  pbl_trace_record(PBL_TRACE_FAULT, 1, 2);
  pbl_trace_freeze();
  pbl_trace_reset();
  cl_assert(!pbl_trace_is_frozen());
  cl_assert_equal_i(pbl_trace_snapshot(s_records, CONFIG_KERNEL_TRACE_RECORDS, NULL), 0);
  pbl_trace_start();
  pbl_trace_record(PBL_TRACE_FAULT, 3, 4);
  cl_assert_equal_i(pbl_trace_snapshot(s_records, CONFIG_KERNEL_TRACE_RECORDS, NULL), 1);
}

void test_trace__fault_during_overwrite_omits_partial_record(void) {
  for (uintptr_t i = 0; i < CONFIG_KERNEL_TRACE_RECORDS; i++) {
    pbl_trace_record(PBL_TRACE_SCHED_WAKE, i, 0);
  }
  s_fault_during_write = true;
  pbl_trace_record(PBL_TRACE_SCHED_WAKE, 99, 0);
  cl_assert(pbl_trace_is_frozen());
  cl_assert_equal_i(pbl_trace_snapshot(s_records, CONFIG_KERNEL_TRACE_RECORDS, NULL),
                    CONFIG_KERNEL_TRACE_RECORDS - 1);
  for (size_t i = 0; i < CONFIG_KERNEL_TRACE_RECORDS - 1; i++) {
    cl_assert_equal_i(s_records[i].arg0, i + 1);
  }
  struct pbl_trace_snapshot_info info;
  cl_assert_equal_i(pbl_trace_snapshot(NULL, 0, &info), 0);
  cl_assert_equal_i(info.available, CONFIG_KERNEL_TRACE_RECORDS - 1);
}
