/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "comm/bt_lock.h"
#include "pbl/services/comm_session/session_analytics.h"
#include "pbl/services/comm_session/session_internal.h"
#include "pbl/services/comm_session/session_send_queue.h"
#include "system/passert.h"
#include "pbl/util/math.h"
#include "pbl/util/size.h"

#include <string.h>

// -------------------------------------------------------------------------------------------------

extern bool comm_session_is_valid(const CommSession *session);

#ifdef CONFIG_COMM_SESSION_QUEUE_RUST
extern void comm_session_queue_accounting_reset(CommSessionQueueAccounting *accounting);
extern bool comm_session_queue_accounting_add(CommSessionQueueAccounting *accounting,
                                              size_t length);
extern bool comm_session_queue_accounting_consume(CommSessionQueueAccounting *accounting,
                                                  size_t length);
extern size_t comm_session_queue_accounting_get(const CommSessionQueueAccounting *accounting);
#endif

// -------------------------------------------------------------------------------------------------
// Interface towards CommSession

void comm_session_send_queue_cleanup(CommSession *session) {
  SessionSendQueueJob *job = session->send_queue_head;
  while (job) {
    SessionSendQueueJob *next = (SessionSendQueueJob *)job->node.next;
    job->impl->free(job);
    job = next;
  }
  session->send_queue_head = NULL;
#ifdef CONFIG_COMM_SESSION_QUEUE_RUST
  session->send_queue_tail = NULL;
  comm_session_queue_accounting_reset(&session->send_queue_accounting);
#endif
}

// -------------------------------------------------------------------------------------------------
// Interface towards Senders

static void prv_add_job(CommSession *session, SessionSendQueueJob *job) {
#ifdef CONFIG_COMM_SESSION_QUEUE_RUST
  PBL_ASSERTN(!job->node.next && !job->node.prev);
  const size_t job_length = job->impl->get_length(job);
  PBL_ASSERTN(comm_session_queue_accounting_add(&session->send_queue_accounting, job_length));
  if (session->send_queue_tail) {
    session->send_queue_tail->node.next = &job->node;
    job->node.prev = &session->send_queue_tail->node;
  } else {
    session->send_queue_head = job;
  }
  session->send_queue_tail = job;
#else
  ListNode *head = (ListNode *)session->send_queue_head;
  PBL_ASSERTN(!list_contains(head, (const ListNode *)job));
  if (head) {
    list_append(head, (ListNode *)job);
  } else {
    session->send_queue_head = job;
  }
#endif
}

void comm_session_send_queue_add_job(CommSession *session, SessionSendQueueJob **job_ptr_ptr) {
  bt_lock();
  {
    SessionSendQueueJob *job = *job_ptr_ptr;
    if (!comm_session_is_valid(session)) {
      job->impl->free(job);
      *job_ptr_ptr = NULL;
      goto unlock;
    }
    prv_add_job(session, job);
    // Schedule to let the transport to send the enqueued data:
    comm_session_send_next(session);
  }
unlock:
  bt_unlock();
}

// -------------------------------------------------------------------------------------------------
// Interface towards Transport
// bt_lock is assumed to be taken by the caller of each of the below functions:

size_t comm_session_send_queue_get_length(const CommSession *session) {
#ifdef CONFIG_COMM_SESSION_QUEUE_RUST
  return comm_session_queue_accounting_get(&session->send_queue_accounting);
#else
  size_t length = 0;
  const SessionSendQueueJob *job = session->send_queue_head;
  while (job) {
    length += job->impl->get_length(job);
    job = (const SessionSendQueueJob *)job->node.next;
  }
  return length;
#endif
}

size_t comm_session_send_queue_copy(CommSession *session, uint32_t start_offset, size_t length,
                                    uint8_t *data_out) {
  size_t remaining_length = length;
  const SessionSendQueueJob *job = session->send_queue_head;
  while (job && remaining_length) {
    const size_t job_length = job->impl->get_length(job);
    if (job_length <= start_offset) {
      start_offset -= job_length;
    } else {
      const size_t copied_length = job->impl->copy(job, start_offset, remaining_length, data_out);
      remaining_length -= copied_length;
      data_out += copied_length;
      start_offset = 0;
    }
    job = (SessionSendQueueJob *)job->node.next;
  }
  return (length - remaining_length);
}

size_t comm_session_send_queue_get_read_pointer(const CommSession *session,
                                                const uint8_t **data_out) {
  if (!session->send_queue_head) {
    return 0;
  }
  const SessionSendQueueJob *job = session->send_queue_head;
  return job->impl->get_read_pointer(job, data_out);
}

void comm_session_send_queue_consume(CommSession *session, size_t remaining_length) {
  // The data has successfully been sent out at this point
  PBL_ASSERTN(session->send_queue_head);
#ifdef CONFIG_COMM_SESSION_QUEUE_RUST
  size_t consumed_length = 0;
#endif
  SessionSendQueueJob *job = session->send_queue_head;
  while (job && remaining_length) {
    const size_t job_length = job->impl->get_length(job);
    const size_t consume_length = MIN(remaining_length, job_length);
    job->impl->consume(job, consume_length);
    SessionSendQueueJob *next = (SessionSendQueueJob *)job->node.next;
    if (job_length == consume_length) {
      // job's done
#ifdef CONFIG_COMM_SESSION_QUEUE_RUST
      list_remove((ListNode *)job, (ListNode **)&session->send_queue_head,
                  (ListNode **)&session->send_queue_tail);
#else
      list_remove((ListNode *)job, (ListNode **)&session->send_queue_head, NULL);
#endif
      job->impl->free(job);
    }
    remaining_length -= consume_length;
#ifdef CONFIG_COMM_SESSION_QUEUE_RUST
    consumed_length += consume_length;
#endif
    job = next;
  }
#ifdef CONFIG_COMM_SESSION_QUEUE_RUST
  PBL_ASSERTN(
      comm_session_queue_accounting_consume(&session->send_queue_accounting, consumed_length));
#endif
}

#ifdef CONFIG_PERFORMANCE_TESTS

#define QUEUE_BENCHMARK_JOB_COUNT 96
#define QUEUE_BENCHMARK_PROBE_COUNT 2048
#define QUEUE_BENCHMARK_COPY_SIZE 16
#define QUEUE_BENCHMARK_EXPECTED_CHECKSUM 4073865016u

typedef struct {
  SessionSendQueueJob job;
  size_t consumed;
  size_t length;
  uint8_t data[8];
} QueueBenchmarkJob;

static QueueBenchmarkJob s_benchmark_jobs[QUEUE_BENCHMARK_JOB_COUNT];
static uint32_t s_benchmark_free_count;

static size_t prv_benchmark_get_length(const SessionSendQueueJob *job) {
  const QueueBenchmarkJob *benchmark_job = (const QueueBenchmarkJob *)job;
  return benchmark_job->length - benchmark_job->consumed;
}

static size_t prv_benchmark_copy(const SessionSendQueueJob *job, int start_offset, size_t length,
                                 uint8_t *data_out) {
  const QueueBenchmarkJob *benchmark_job = (const QueueBenchmarkJob *)job;
  const size_t available = prv_benchmark_get_length(job) - start_offset;
  const size_t copy_length = MIN(available, length);
  memcpy(data_out, benchmark_job->data + benchmark_job->consumed + start_offset, copy_length);
  return copy_length;
}

static size_t prv_benchmark_get_read_pointer(const SessionSendQueueJob *job,
                                             const uint8_t **data_out) {
  const QueueBenchmarkJob *benchmark_job = (const QueueBenchmarkJob *)job;
  *data_out = benchmark_job->data + benchmark_job->consumed;
  return prv_benchmark_get_length(job);
}

static void prv_benchmark_consume(const SessionSendQueueJob *job, size_t length) {
  QueueBenchmarkJob *benchmark_job = (QueueBenchmarkJob *)job;
  benchmark_job->consumed += length;
}

static void prv_benchmark_free(SessionSendQueueJob *job) {
  (void)job;
  ++s_benchmark_free_count;
}

static const SessionSendJobImpl s_benchmark_job_impl = {
    .get_length = prv_benchmark_get_length,
    .copy = prv_benchmark_copy,
    .get_read_pointer = prv_benchmark_get_read_pointer,
    .consume = prv_benchmark_consume,
    .free = prv_benchmark_free,
};

RtcTicks comm_session_send_queue_benchmark(uint32_t *checksum_out) {
  CommSession session = {};
  s_benchmark_free_count = 0;

  const RtcTicks start = rtc_get_ticks();
  for (size_t i = 0; i < ARRAY_LENGTH(s_benchmark_jobs); ++i) {
    QueueBenchmarkJob *job = &s_benchmark_jobs[i];
    *job = (QueueBenchmarkJob){
        .job.impl = &s_benchmark_job_impl,
        .length = 1 + (i % sizeof(job->data)),
    };
    for (size_t j = 0; j < job->length; ++j) {
      job->data[j] = (i * 17) + j;
    }
    prv_add_job(&session, &job->job);
  }

  const size_t queue_length = comm_session_send_queue_get_length(&session);
  uint32_t checksum = 2166136261u;
  uint8_t buffer[QUEUE_BENCHMARK_COPY_SIZE];
  for (size_t i = 0; i < QUEUE_BENCHMARK_PROBE_COUNT; ++i) {
    PBL_ASSERTN(comm_session_send_queue_get_length(&session) == queue_length);
    const uint32_t offset = (i * 29) % (queue_length - sizeof(buffer));
    PBL_ASSERTN(comm_session_send_queue_copy(&session, offset, sizeof(buffer), buffer) ==
                sizeof(buffer));
    for (size_t j = 0; j < sizeof(buffer); ++j) {
      checksum = (checksum ^ buffer[j]) * 16777619u;
    }
  }

  comm_session_send_queue_consume(&session, queue_length);
  PBL_ASSERTN(!session.send_queue_head);
  PBL_ASSERTN(s_benchmark_free_count == ARRAY_LENGTH(s_benchmark_jobs));
  PBL_ASSERTN(checksum == QUEUE_BENCHMARK_EXPECTED_CHECKSUM);
  *checksum_out = checksum;
  return rtc_get_ticks() - start;
}

#endif
