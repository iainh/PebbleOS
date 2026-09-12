/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum pbl_trace_category {
  PBL_TRACE_CAT_SCHED = 1u << 0,
  PBL_TRACE_CAT_MUTEX = 1u << 1,
  PBL_TRACE_CAT_QUEUE = 1u << 2,
  PBL_TRACE_CAT_IDLE = 1u << 3,
  PBL_TRACE_CAT_TIMER = 1u << 4,
  PBL_TRACE_CAT_FAULT = 1u << 5,
  PBL_TRACE_CAT_ALL = (1u << 6) - 1,
};

enum pbl_trace_event {
  PBL_TRACE_SCHED_SWITCH = (PBL_TRACE_CAT_SCHED << 8) | 1,
  PBL_TRACE_SCHED_BLOCK = (PBL_TRACE_CAT_SCHED << 8) | 2,
  PBL_TRACE_SCHED_WAKE = (PBL_TRACE_CAT_SCHED << 8) | 3,
  PBL_TRACE_SCHED_PRIORITY = (PBL_TRACE_CAT_SCHED << 8) | 4,
  PBL_TRACE_MUTEX_CONTENTION = (PBL_TRACE_CAT_MUTEX << 8) | 1,
  PBL_TRACE_QUEUE_FULL = (PBL_TRACE_CAT_QUEUE << 8) | 1,
  PBL_TRACE_IDLE_REQUEST = (PBL_TRACE_CAT_IDLE << 8) | 1,
  PBL_TRACE_IDLE_ELAPSED = (PBL_TRACE_CAT_IDLE << 8) | 2,
  PBL_TRACE_TIMER_LATENESS = (PBL_TRACE_CAT_TIMER << 8) | 1,
  PBL_TRACE_FAULT = (PBL_TRACE_CAT_FAULT << 8) | 1,
};

struct pbl_trace_record {
  uint32_t sequence;
  uint32_t timestamp;
  uint32_t thread_id;
  uint16_t event;
  uint16_t reserved;
  uintptr_t arg0;
  uintptr_t arg1;
};

struct pbl_trace_snapshot_info {
  uint32_t overwritten;
  uint32_t newest_sequence;
  size_t available;
};

#if defined(CONFIG_KERNEL_TRACE) && CONFIG_KERNEL_TRACE
void pbl_trace_reset(void);
void pbl_trace_start(void);
void pbl_trace_stop(void);
void pbl_trace_freeze(void);
bool pbl_trace_is_frozen(void);
void pbl_trace_set_mask(uint32_t categories);
uint32_t pbl_trace_get_mask(void);
void pbl_trace_record(enum pbl_trace_event event, uintptr_t arg0, uintptr_t arg1);
//! Kernel-only producer: caller already masks kernel-capable interrupts.
void pbl_trace_record_locked(enum pbl_trace_event event, uintptr_t arg0, uintptr_t arg1);
size_t pbl_trace_snapshot(struct pbl_trace_record *records, size_t capacity,
                          struct pbl_trace_snapshot_info *info);
#else
static inline void pbl_trace_reset(void) {}
static inline void pbl_trace_start(void) {}
static inline void pbl_trace_stop(void) {}
static inline void pbl_trace_freeze(void) {}
static inline bool pbl_trace_is_frozen(void) {
  return false;
}
static inline void pbl_trace_set_mask(uint32_t categories) {
  (void)categories;
}
static inline uint32_t pbl_trace_get_mask(void) {
  return 0;
}
static inline void pbl_trace_record(enum pbl_trace_event event, uintptr_t arg0, uintptr_t arg1) {
  (void)event;
  (void)arg0;
  (void)arg1;
}
static inline void pbl_trace_record_locked(enum pbl_trace_event event, uintptr_t arg0,
                                           uintptr_t arg1) {
  (void)event;
  (void)arg0;
  (void)arg1;
}
static inline size_t pbl_trace_snapshot(struct pbl_trace_record *records, size_t capacity,
                                        struct pbl_trace_snapshot_info *info) {
  (void)records;
  (void)capacity;
  if (info) {
    *info = (struct pbl_trace_snapshot_info){0};
  }
  return 0;
}
#endif
