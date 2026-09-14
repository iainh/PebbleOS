/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "clar.h"

// The generic host test configuration is 1024 Hz; SF32 runs at 1000 Hz.
#undef CONFIG_KERNEL_TICK_HZ
#define CONFIG_KERNEL_TICK_HZ 1000
#include "soc/sf32lb/sf32lb52x/tickless.h"

void test_tickless__one_deadline_replaces_periodic_interrupts(void) {
  // 499 full ticks plus the 731 cycles left in the current tick.
  cl_assert_equal_i(sf32lb_idle_cycles(500, 731), 958811);
  cl_assert_equal_i(sf32lb_idle_cycles(2, 731), 2651);
}

void test_tickless__early_wake_retains_partial_tick(void) {
  uint32_t remaining;
  // Already 1189 cycles into a tick; wake after another 600 cycles.
  cl_assert_equal_i(sf32lb_idle_elapsed(958811, 731, 958211, false, &remaining), 0);
  cl_assert_equal_i(remaining, 131);
  // Cross the tick boundary by one cycle, rather than rounding the sleep up.
  cl_assert_equal_i(sf32lb_idle_elapsed(958811, 731, 958079, false, &remaining), 1);
  cl_assert_equal_i(remaining, 1919);
}

void test_tickless__expiry_and_post_reload_latency(void) {
  uint32_t remaining;
  cl_assert_equal_i(sf32lb_idle_elapsed(958811, 731, 0, true, &remaining), 500);
  cl_assert_equal_i(remaining, 1920);
  // 17 cycles of interrupt latency after the long interval reloaded.
  cl_assert_equal_i(sf32lb_idle_elapsed(958811, 731, 958794, true, &remaining), 500);
  cl_assert_equal_i(remaining, 1903);
}

void test_tickless__all_partial_tick_boundaries(void) {
  // Exercise every possible phase, including the one- and two-cycle remainders.
  for (uint32_t phase = 1; phase <= 1920; phase++) {
    uint32_t remaining;
    uint32_t period = sf32lb_idle_cycles(7, phase);
    cl_assert_equal_i(sf32lb_idle_elapsed(period, phase, 1, false, &remaining), 6);
    cl_assert_equal_i(remaining, 1);
    cl_assert_equal_i(sf32lb_idle_elapsed(period, phase, 2, false, &remaining), 6);
    cl_assert_equal_i(remaining, 2);
    cl_assert_equal_i(sf32lb_idle_elapsed(period, phase, 0, true, &remaining), 7);
    cl_assert_equal_i(remaining, 1920);
  }
}
