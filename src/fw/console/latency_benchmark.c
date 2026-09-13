/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "console/latency_benchmark.h"

#ifdef CONFIG_PERFORMANCE_TESTS

#include "applib/graphics/framebuffer.h"
#include "console/prompt.h"
#include "kernel/pbl_malloc.h"
#include "pbl/drivers/rtc.h"
#include "pbl/kernel/irq.h"
#include "pbl/util/heap.h"
#include "pbl/services/new_timer/new_timer.h"
#include "pbl/services/notifications/notifications.h"
#include "pbl/services/notifications/notification_storage.h"
#include "pbl/services/notifications/notification_storage_private.h"
#include "pbl/services/compositor/compositor.h"
#include "pbl/services/compositor/compositor_display.h"
#include "pbl/services/comm_session/session_send_queue.h"
#include "pbl/services/system_task.h"
#include "pbl/services/timeline/timeline.h"
#include "pbl/util/uuid.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#define LATENCY_BENCHMARK_TIMEOUT_MS (10 * 1000)

typedef enum {
  LatencyBenchmarkStateIdle,
  LatencyBenchmarkStateArmed,
  LatencyBenchmarkStateReceived,
  LatencyBenchmarkStateStored,
  LatencyBenchmarkStateUiHandled,
  LatencyBenchmarkStateDisplayStarted,
  LatencyBenchmarkStateComplete,
  LatencyBenchmarkStateTimedOut,
} LatencyBenchmarkState;

typedef struct {
  LatencyBenchmarkState state;
  LatencyBenchmarkState timeout_state;
  RtcTicks received;
  RtcTicks stored;
  RtcTicks ui_handled;
  RtcTicks display_started;
  RtcTicks display_complete;
} LatencyBenchmark;

static LatencyBenchmark s_benchmark;
static TimerID s_timeout_timer = TIMER_INVALID_ID;

static uint64_t prv_ticks_to_us(RtcTicks ticks) {
  return ticks * 1000000 / RTC_TICKS_HZ;
}

static void prv_finish(void *data) {
  (void)data;

  LatencyBenchmark result;
  pbl_irq_lock();
  result = s_benchmark;
  s_benchmark.state = LatencyBenchmarkStateIdle;
  pbl_irq_unlock();

  if (s_timeout_timer != TIMER_INVALID_ID) {
    new_timer_stop(s_timeout_timer);
  }

  if (result.state == LatencyBenchmarkStateComplete) {
    char buffer[208];
    prompt_send_response_fmt(buffer, sizeof(buffer),
                             "LATENCY_RESULT version=1 total_us=%" PRIu64 " storage_us=%" PRIu64
                             " dispatch_us=%" PRIu64 " render_us=%" PRIu64 " flush_us=%" PRIu64
                             " rows=%u",
                             prv_ticks_to_us(result.display_complete - result.received),
                             prv_ticks_to_us(result.stored - result.received),
                             prv_ticks_to_us(result.ui_handled - result.stored),
                             prv_ticks_to_us(result.display_started - result.ui_handled),
                             prv_ticks_to_us(result.display_complete - result.display_started),
                             compositor_display_get_last_row_count());
  } else {
    char buffer[80];
    prompt_send_response_fmt(buffer, sizeof(buffer), "LATENCY_ERROR timeout state=%u",
                             result.timeout_state);
  }
  prompt_command_finish();
}

static void prv_timeout(void *data) {
  (void)data;
  bool should_finish = false;
  pbl_irq_lock();
  if (s_benchmark.state != LatencyBenchmarkStateIdle &&
      s_benchmark.state != LatencyBenchmarkStateComplete) {
    s_benchmark.timeout_state = s_benchmark.state;
    s_benchmark.state = LatencyBenchmarkStateTimedOut;
    should_finish = true;
  }
  pbl_irq_unlock();
  if (should_finish) {
    system_task_add_callback(prv_finish, NULL);
  }
}

static void prv_arm(void) {
  pbl_irq_lock();
  s_benchmark = (LatencyBenchmark){
      .state = LatencyBenchmarkStateArmed,
  };
  pbl_irq_unlock();

  if (s_timeout_timer == TIMER_INVALID_ID) {
    s_timeout_timer = new_timer_create();
  }
  new_timer_start(s_timeout_timer, LATENCY_BENCHMARK_TIMEOUT_MS, prv_timeout, NULL, 0);
  prompt_command_continues_after_returning();
}

static TimelineItem *prv_create_synthetic_notification(void) {
  AttributeList attributes = {};
  attribute_list_add_cstring(&attributes, AttributeIdTitle, "Latency benchmark");
  attribute_list_add_cstring(
      &attributes, AttributeIdBody,
      "Measures notification persistence, dispatch, rendering, and display completion.");

  TimelineItem *item = timeline_item_create_with_attributes(
      rtc_get_time(), 0, TimelineItemTypeNotification, LayoutIdNotification, &attributes, NULL);
  attribute_list_destroy_list(&attributes);
  return item;
}

static void prv_add_synthetic_notification(void) {
  TimelineItem *item = prv_create_synthetic_notification();
  notifications_add_notification(item);
  timeline_item_destroy(item);
}

static void prv_run_storage_benchmark(void) {
  notification_storage_reset_and_init();

  TimelineItem *item = prv_create_synthetic_notification();
  const size_t record_size =
      sizeof(SerializedTimelineItemHeader) + timeline_item_get_serialized_payload_size(item);
  const size_t record_count = NOTIFICATION_STORAGE_FILE_SIZE / record_size;
  for (size_t i = 0; i < record_count; ++i) {
    uuid_generate(&item->header.id);
    item->header.status = 0;
    notification_storage_store(item);
  }

  uuid_generate(&item->header.id);
  item->header.status = 0;
  prv_arm();
  notifications_add_notification(item);
  timeline_item_destroy(item);
}

static void prv_run_damage_benchmark(void) {
  const RtcTicks now = rtc_get_ticks();
  pbl_irq_lock();
  s_benchmark = (LatencyBenchmark){
      .state = LatencyBenchmarkStateUiHandled,
      .received = now,
      .stored = now,
      .ui_handled = now,
  };
  pbl_irq_unlock();

  if (s_timeout_timer == TIMER_INVALID_ID) {
    s_timeout_timer = new_timer_create();
  }
  new_timer_start(s_timeout_timer, LATENCY_BENCHMARK_TIMEOUT_MS, prv_timeout, NULL, 0);
  prompt_command_continues_after_returning();

  FrameBuffer *framebuffer = compositor_get_framebuffer();
  framebuffer_reset_dirty(framebuffer);
  framebuffer_mark_dirty_rect(framebuffer, GRect(0, 8, 1, 1));
  framebuffer_mark_dirty_rect(framebuffer, GRect(0, framebuffer->size.h - 9, 1, 1));
  compositor_display_update(NULL);
}

void command_latency_benchmark(const char *mode) {
  if (s_benchmark.state != LatencyBenchmarkStateIdle) {
    prompt_send_response("LATENCY_ERROR benchmark already active");
    return;
  }

  if (strcmp(mode, "synthetic") == 0) {
    prv_arm();
    prv_add_synthetic_notification();
  } else if (strcmp(mode, "storage") == 0) {
    prv_run_storage_benchmark();
  } else if (strcmp(mode, "arm") == 0) {
    prv_arm();
    prompt_send_response("LATENCY_ARMED waiting for notification");
  } else if (strcmp(mode, "damage") == 0) {
    prv_run_damage_benchmark();
  } else if (strcmp(mode, "queue") == 0) {
    uint32_t checksum;
    const RtcTicks elapsed = comm_session_send_queue_benchmark(&checksum);
    char buffer[128];
    prompt_send_response_fmt(buffer, sizeof(buffer),
                             "QUEUE_RESULT version=1 total_us=%" PRIu64 " checksum=%" PRIu32
                             " jobs=96 probes=2048",
                             prv_ticks_to_us(elapsed), checksum);
  } else if (strcmp(mode, "heap") == 0) {
    uint32_t checksum;
    uint32_t stable_reallocs;
    const RtcTicks elapsed = heap_allocator_benchmark(&checksum, &stable_reallocs);
    char buffer[128];
    prompt_send_response_fmt(buffer, sizeof(buffer),
                             "HEAP_RESULT version=1 total_us=%" PRIu64 " checksum=%" PRIu32
                             " cycles=2048 stable=%" PRIu32,
                             prv_ticks_to_us(elapsed), checksum, stable_reallocs);
  } else {
    prompt_send_response("Usage: latency benchmark synthetic|storage|arm|damage|queue|heap");
  }
}

void latency_benchmark_notification_received(void) {
  pbl_irq_lock();
  if (s_benchmark.state == LatencyBenchmarkStateArmed) {
    s_benchmark.received = rtc_get_ticks();
    s_benchmark.state = LatencyBenchmarkStateReceived;
  }
  pbl_irq_unlock();
}

void latency_benchmark_notification_stored(void) {
  pbl_irq_lock();
  if (s_benchmark.state == LatencyBenchmarkStateReceived) {
    s_benchmark.stored = rtc_get_ticks();
    s_benchmark.state = LatencyBenchmarkStateStored;
  }
  pbl_irq_unlock();
}

void latency_benchmark_notification_ui_handled(void) {
  pbl_irq_lock();
  if (s_benchmark.state == LatencyBenchmarkStateStored) {
    s_benchmark.ui_handled = rtc_get_ticks();
    s_benchmark.state = LatencyBenchmarkStateUiHandled;
  }
  pbl_irq_unlock();
}

void latency_benchmark_display_update_started(void) {
  pbl_irq_lock();
  if (s_benchmark.state == LatencyBenchmarkStateUiHandled) {
    s_benchmark.display_started = rtc_get_ticks();
    s_benchmark.state = LatencyBenchmarkStateDisplayStarted;
  }
  pbl_irq_unlock();
}

void latency_benchmark_display_update_complete(void) {
  bool should_finish = false;
  pbl_irq_lock();
  if (s_benchmark.state == LatencyBenchmarkStateDisplayStarted) {
    s_benchmark.display_complete = rtc_get_ticks();
    s_benchmark.state = LatencyBenchmarkStateComplete;
    should_finish = true;
  }
  pbl_irq_unlock();

  if (should_finish) {
    if (pbl_in_isr()) {
      bool should_context_switch = false;
      system_task_add_callback_from_isr(prv_finish, NULL, &should_context_switch);
    } else {
      system_task_add_callback(prv_finish, NULL);
    }
  }
}

#endif
