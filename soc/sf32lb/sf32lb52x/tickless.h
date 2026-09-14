/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

// SysTick uses HXT48 / 25 even when HCLK changes in deep WFI.
#define SF32LB_TICK_CYCLES (1920000U / CONFIG_KERNEL_TICK_HZ)

static inline uint32_t sf32lb_idle_cycles(uint32_t ticks, uint32_t remaining) {
  return (ticks - 1) * SF32LB_TICK_CYCLES + remaining;
}

// Counter stopped after WFI. At zero COUNTFLAG describes the first expiry;
// otherwise it describes an expiry followed by an automatic reload.
static inline uint32_t sf32lb_idle_elapsed(uint32_t period, uint32_t initial_remaining,
                                           uint32_t value, bool wrapped, uint32_t *remaining) {
  uint32_t cycles = period - value;
  if (wrapped && value) {
    cycles += period;
  }
  cycles += SF32LB_TICK_CYCLES - initial_remaining;
  *remaining = SF32LB_TICK_CYCLES - cycles % SF32LB_TICK_CYCLES;
  return cycles / SF32LB_TICK_CYCLES;
}
