/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "pbl/util/math_fixed.h"

#include <stdint.h>

#define KRAEPELIN_PIM_INPUT_STATE_SIZE 5
#define KRAEPELIN_PIM_OUTPUT_STATE_SIZE 4

int32_t kraepelin_pim_sum(const int16_t *samples, int num_samples,
                          Fixed_S64_32 state_x[KRAEPELIN_PIM_INPUT_STATE_SIZE],
                          Fixed_S64_32 state_y[KRAEPELIN_PIM_OUTPUT_STATE_SIZE]);
