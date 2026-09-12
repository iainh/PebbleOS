/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#define KRAEPELIN_TRANSFORM_AXES 3
#define KRAEPELIN_TRANSFORM_WIDTH 128
#define KRAEPELIN_TRANSFORM_MAGNITUDES (KRAEPELIN_TRANSFORM_WIDTH / 2)

void kraepelin_transform_window(int16_t samples[KRAEPELIN_TRANSFORM_WIDTH], uint16_t num_samples);
void kraepelin_transform_fft(int16_t samples[KRAEPELIN_TRANSFORM_WIDTH]);
void kraepelin_transform_magnitudes(int16_t samples[KRAEPELIN_TRANSFORM_WIDTH]);
void kraepelin_transform_epoch(int16_t samples[KRAEPELIN_TRANSFORM_AXES][KRAEPELIN_TRANSFORM_WIDTH],
                               uint16_t num_samples,
                               int16_t magnitudes[KRAEPELIN_TRANSFORM_MAGNITUDES]);
