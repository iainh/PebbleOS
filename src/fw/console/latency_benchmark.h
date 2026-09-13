/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#ifdef CONFIG_PERFORMANCE_TESTS

void command_latency_benchmark(const char *mode);
void latency_benchmark_notification_received(void);
void latency_benchmark_notification_stored(void);
void latency_benchmark_notification_ui_handled(void);
void latency_benchmark_display_update_started(void);
void latency_benchmark_display_update_complete(void);

#else

#define latency_benchmark_notification_received()
#define latency_benchmark_notification_stored()
#define latency_benchmark_notification_ui_handled()
#define latency_benchmark_display_update_started()
#define latency_benchmark_display_update_complete()

#endif
