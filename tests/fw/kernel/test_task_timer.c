/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "kernel/task_timer.h"
#include "kernel/task_timer_manager.h"

#include "clar.h"
#include "fakes/fake_mutex.h"
#include "fakes/fake_pebble_tasks.h"
#include "fakes/fake_rtc.h"
#include "fakes/fake_sem.h"
#include "stubs_logging.h"

#include <string.h>

static TaskTimerManager s_manager;
static struct pbl_sem s_sem = PBL_SEM_INITIALIZER(0, 1);
static unsigned int s_calls[4];
static unsigned int s_order[4];
static unsigned int s_order_count;
static TaskTimerID s_timer_ids[8];
static unsigned int s_timer_count;
static TaskTimerID s_executing_timer;
static bool s_executing_start_result;

void passert_failed_no_message(const char *filename, int line_number) {
  cl_assert_(false, filename);
  abort();
}

static void prv_callback(void *data) {
  unsigned int index = (uintptr_t)data;
  s_calls[index]++;
  s_order[s_order_count++] = index;
  if (index == 3) {
    s_executing_start_result =
        task_timer_start_with_slack(&s_manager, s_executing_timer, 500, 500, prv_callback, data,
                                    TIMER_START_FLAG_FAIL_IF_EXECUTING);
  }
}

static TaskTimerID prv_start(uint32_t timeout_ms, uint32_t slack_ms, unsigned int index,
                             uint32_t flags) {
  TaskTimerID id = task_timer_create(&s_manager);
  s_timer_ids[s_timer_count++] = id;
  cl_assert(task_timer_start_with_slack(&s_manager, id, timeout_ms, slack_ms, prv_callback,
                                        (void *)(uintptr_t)index, flags));
  return id;
}

void test_task_timer__initialize(void) {
  fake_mutex_reset(true);
  fake_sem_reset();
  fake_rtc_init(0, 0);
  memset(s_calls, 0, sizeof(s_calls));
  memset(s_order, 0, sizeof(s_order));
  s_order_count = 0;
  s_timer_count = 0;
  s_executing_start_result = true;
  task_timer_manager_init(&s_manager, &s_sem);
}

void test_task_timer__cleanup(void) {
  for (unsigned int i = 0; i < s_timer_count; i++) {
    task_timer_delete(&s_manager, s_timer_ids[i]);
  }
  fake_mutex_reset(true);
  fake_sem_reset();
}

void test_task_timer__window_edges_and_strict_timer(void) {
  prv_start(1000, 500, 0, 0);
  prv_start(1500, 0, 1, 0);

  cl_assert_equal_i(task_timer_manager_execute_expired_timers(&s_manager), 1536);
  fake_rtc_set_ticks(1535);
  cl_assert_equal_i(task_timer_manager_execute_expired_timers(&s_manager), 1);
  cl_assert_equal_i(s_order_count, 0);
  fake_rtc_set_ticks(1536);
  task_timer_manager_execute_expired_timers(&s_manager);
  cl_assert_equal_i(s_order_count, 2);
  cl_assert_equal_i(s_order[0], 0);
  cl_assert_equal_i(s_order[1], 1);
}

void test_task_timer__outside_window_and_insertion_order(void) {
  prv_start(1501, 0, 1, 0);
  prv_start(1000, 500, 0, 0);
  cl_assert_equal_i(task_timer_manager_execute_expired_timers(&s_manager), 1024);

  fake_rtc_set_ticks(1024);
  task_timer_manager_execute_expired_timers(&s_manager);
  cl_assert_equal_i(s_order_count, 1);
  cl_assert_equal_i(s_order[0], 0);
}

void test_task_timer__repeat_keeps_nominal_deadline(void) {
  prv_start(1000, 500, 0, TIMER_START_FLAG_REPEATING);
  prv_start(1500, 0, 1, 0);
  fake_rtc_set_ticks(1536);
  task_timer_manager_execute_expired_timers(&s_manager);

  // The next nominal deadline is 2000 ms, not 2500 ms after the delayed callback.
  cl_assert_equal_i(task_timer_manager_execute_expired_timers(&s_manager), 512);
  fake_rtc_set_ticks(2048);
  task_timer_manager_execute_expired_timers(&s_manager);
  cl_assert_equal_i(s_calls[0], 2);
}

void test_task_timer__cancel_reschedule_and_fail_flags(void) {
  TaskTimerID id = prv_start(1000, 500, 0, 0);
  cl_assert(!task_timer_start_with_slack(&s_manager, id, 2000, 500, prv_callback, NULL,
                                         TIMER_START_FLAG_FAIL_IF_SCHEDULED));
  cl_assert(task_timer_stop(&s_manager, id));
  cl_assert_equal_i(task_timer_manager_execute_expired_timers(&s_manager), PBL_TICK_FOREVER);

  cl_assert(task_timer_start_with_slack(&s_manager, id, 2000, 500, prv_callback, NULL, 0));
  cl_assert(
      task_timer_start_with_slack(&s_manager, id, 500, 0, prv_callback, (void *)(uintptr_t)3, 0));
  s_executing_timer = id;
  fake_rtc_set_ticks(512);
  task_timer_manager_execute_expired_timers(&s_manager);
  cl_assert(!s_executing_start_result);
}

void test_task_timer__strict_api_and_no_unnecessary_wakeup(void) {
  TaskTimerID id = prv_start(1000, 500, 0, 0);
  cl_assert(task_timer_start(&s_manager, id, 1000, prv_callback, NULL, 0));
  pbl_sem_take(&s_sem, PBL_NO_WAIT);
  prv_start(1500, 0, 1, 0);
  cl_assert_equal_i(pbl_sem_take(&s_sem, PBL_NO_WAIT), -EBUSY);
  fake_rtc_set_ticks(1023);
  cl_assert_equal_i(task_timer_manager_execute_expired_timers(&s_manager), 1);
  cl_assert_equal_i(s_order_count, 0);
  fake_rtc_set_ticks(1024);
  cl_assert_equal_i(task_timer_manager_execute_expired_timers(&s_manager), 512);
  cl_assert_equal_i(s_calls[0], 1);
  cl_assert_equal_i(s_calls[1], 0);
}

void test_task_timer__overlap_cannot_extend_earliest_window(void) {
  prv_start(1750, 0, 2, 0);
  prv_start(1500, 1000, 1, 0);
  prv_start(1000, 500, 0, 0);
  cl_assert_equal_i(task_timer_manager_execute_expired_timers(&s_manager), 1536);
  fake_rtc_set_ticks(1536);
  cl_assert_equal_i(task_timer_manager_execute_expired_timers(&s_manager), 256);
  cl_assert_equal_i(s_calls[0], 1);
  cl_assert_equal_i(s_calls[2], 0);
}

void test_task_timer__removing_anchor_recomputes_wakeup(void) {
  prv_start(1000, 500, 0, 0);
  TaskTimerID anchor = prv_start(1500, 0, 1, 0);
  pbl_sem_take(&s_sem, PBL_NO_WAIT);
  cl_assert(task_timer_stop(&s_manager, anchor));
  cl_assert_equal_i(pbl_sem_take(&s_sem, PBL_NO_WAIT), 0);
  cl_assert_equal_i(task_timer_manager_execute_expired_timers(&s_manager), 1024);
}
