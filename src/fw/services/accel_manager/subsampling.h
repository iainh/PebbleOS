/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

uint32_t accel_subsampling_plan(uint32_t numerator, uint32_t denominator, uint32_t *state,
                                uint16_t available, uint16_t *selected,
                                uint16_t selected_capacity);

//! Run a deterministic fixed-ratio CPU workload. The return value is an
//! integrity checksum, allowing callers to prevent dead-code elimination.
uint32_t accel_manager_subsampling_benchmark(uint32_t iterations);
