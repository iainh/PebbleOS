/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "pbl/kernel/trace.h"

#include "pbl/kernel/irq.h"
#include "pbl/kernel/sched.h"
#include "pbl/kernel/thread.h"

#if defined(CONFIG_KERNEL_TRACE) && CONFIG_KERNEL_TRACE
static struct pbl_trace_record s_records[CONFIG_KERNEL_TRACE_RECORDS];
static volatile bool s_running = true;
static volatile bool s_frozen;
static volatile uint32_t s_mask = PBL_TRACE_CAT_ALL;
static volatile uint32_t s_next_sequence = 1;
static volatile uint32_t s_overwritten;
static volatile size_t s_next_slot;

void pbl_trace_reset(void) {
  pbl_irq_lock();
  s_running = false;
  s_frozen = false;
  s_next_sequence = 1;
  s_overwritten = 0;
  s_next_slot = 0;
  for (size_t i = 0; i < CONFIG_KERNEL_TRACE_RECORDS; ++i) {
    s_records[i].sequence = 0;
  }
  pbl_irq_unlock();
}

void pbl_trace_start(void) {
  pbl_irq_lock();
  if (!s_frozen) {
    s_running = true;
  }
  pbl_irq_unlock();
}

void pbl_trace_stop(void) {
  pbl_irq_lock();
  s_running = false;
  pbl_irq_unlock();
}

void pbl_trace_freeze(void) {
  // A fault may interrupt a producer. Stop it without waiting for its lock;
  // its zero commit marker makes the partially written slot invisible.
  s_running = false;
  s_frozen = true;
}

bool pbl_trace_is_frozen(void) {
  return s_frozen;
}

void pbl_trace_set_mask(uint32_t categories) {
  pbl_irq_lock();
  s_mask = categories & PBL_TRACE_CAT_ALL;
  pbl_irq_unlock();
}

uint32_t pbl_trace_get_mask(void) {
  return s_mask;
}

void pbl_trace_record(enum pbl_trace_event event, uintptr_t arg0, uintptr_t arg1) {
  const uint32_t category = ((uint32_t)event >> 8) & 0xffu;
  if (!s_running || s_frozen || !(s_mask & category)) {
    return;
  }

  pbl_irq_lock();
  pbl_trace_record_locked(event, arg0, arg1);
  pbl_irq_unlock();
}

void pbl_trace_record_locked(enum pbl_trace_event event, uintptr_t arg0, uintptr_t arg1) {
  const uint32_t category = ((uint32_t)event >> 8) & 0xffu;
  if (s_running && !s_frozen && (s_mask & category)) {
    const uint32_t sequence = s_next_sequence++;
    if (!s_next_sequence) {
      s_next_sequence = 1;
    }
    struct pbl_trace_record *record = &s_records[s_next_slot];
    if (record->sequence != 0) {
      ++s_overwritten;
    }
    record->sequence = 0;
    __asm__ volatile("" ::: "memory");
    s_next_slot = (s_next_slot + 1) % CONFIG_KERNEL_TRACE_RECORDS;
    record->timestamp = pbl_uptime_ticks();
    struct pbl_thread *thread = pbl_thread_current();
    record->thread_id = thread ? pbl_thread_id(thread) : 0;
    record->event = (uint16_t)event;
    record->reserved = 0;
    record->arg0 = arg0;
    record->arg1 = arg1;
    __asm__ volatile("" ::: "memory");
    if (!s_frozen) {
      record->sequence = sequence;
    }
  }
}

size_t pbl_trace_snapshot(struct pbl_trace_record *records, size_t capacity,
                          struct pbl_trace_snapshot_info *info) {
  const bool lock = !s_frozen;
  if (lock) {
    pbl_irq_lock();
  }

  size_t available = 0;
  uint32_t newest = 0;
  for (size_t n = 0; n < CONFIG_KERNEL_TRACE_RECORDS; ++n) {
    size_t i = (s_next_slot + n) % CONFIG_KERNEL_TRACE_RECORDS;
    uint32_t sequence = s_records[i].sequence;
    if (sequence) {
      ++available;
      newest = sequence;
    }
  }

  const size_t wanted = capacity < available ? capacity : available;
  size_t skip = available - wanted;
  size_t copied = 0;
  for (size_t n = 0; n < CONFIG_KERNEL_TRACE_RECORDS; ++n) {
    size_t i = (s_next_slot + n) % CONFIG_KERNEL_TRACE_RECORDS;
    if (!s_records[i].sequence) {
      continue;
    }
    if (skip) {
      skip--;
    } else {
      records[copied++] = s_records[i];
    }
  }
  if (info) {
    *info = (struct pbl_trace_snapshot_info){
        .overwritten = s_overwritten,
        .newest_sequence = newest,
        .available = available,
    };
  }
  if (lock) {
    pbl_irq_unlock();
  }
  return copied;
}
#endif
