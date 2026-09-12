/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <string.h>

#include "clar.h"
#include "pbl/util/trig.h"
#include "services/activity/kraepelin/kraepelin_transform.h"

#include "kraepelin_reference/fourier.h"
#include "kraepelin_reference/helper_worker.h"

static uint32_t s_random_state;

static int16_t prv_random_sample(void) {
  s_random_state = s_random_state * 1664525 + 1013904223;
  return (int16_t)((s_random_state >> 16) % 2001) - 1000;
}

static int32_t prv_mean(const int16_t *samples, uint16_t num_samples) {
  int32_t sum = 0;
  for (uint16_t i = 0; i < num_samples; i++) {
    sum += samples[i];
  }
  return sum / num_samples;
}

static void prv_reference_window(int16_t *samples, uint16_t num_samples) {
  const int32_t mean = prv_mean(samples, num_samples);
  for (uint16_t i = 0; i < num_samples; i++) {
    samples[i] =
        (int16_t)(((samples[i] - mean) * sin_lookup((TRIG_MAX_ANGLE * i) / (2 * num_samples))) /
                  TRIG_MAX_RATIO);
  }
}

static void prv_fill_asymmetric(int16_t *samples, uint16_t num_samples) {
  for (uint16_t i = 0; i < num_samples; i++) {
    samples[i] = prv_random_sample() + (int16_t)(3 * i) - 127;
  }
}

static void prv_assert_equal(const int16_t *expected, const int16_t *actual, uint16_t count) {
  cl_assert_equal_m(expected, actual, count * sizeof(*actual));
}

static void prv_reference_epoch(
    int16_t samples[KRAEPELIN_TRANSFORM_AXES][KRAEPELIN_TRANSFORM_WIDTH], uint16_t num_samples,
    int16_t magnitudes[KRAEPELIN_TRANSFORM_MAGNITUDES]) {
  for (uint16_t axis = 0; axis < KRAEPELIN_TRANSFORM_AXES; axis++) {
    prv_reference_window(samples[axis], num_samples);
    for (uint16_t i = 0; i < num_samples; i++) {
      samples[axis][i] /= 2;
    }
    const int16_t mean = prv_mean(samples[axis], num_samples);
    for (uint16_t i = 0; i < num_samples; i++) {
      samples[axis][i] -= mean;
    }
    for (uint16_t i = num_samples; i < KRAEPELIN_TRANSFORM_WIDTH; i++) {
      samples[axis][i] = 0;
    }
    fft_2radix_real(samples[axis], 7);
    fft_mag(samples[axis], 7);
  }
  for (uint16_t i = 0; i < KRAEPELIN_TRANSFORM_MAGNITUDES; i++) {
    magnitudes[i] = (int16_t)isqrt(samples[0][i] * samples[0][i] + samples[1][i] * samples[1][i] +
                                   samples[2][i] * samples[2][i]);
  }
}

void test_kraepelin_transform__initialize(void) {
  s_random_state = 0x51f15eed;
}

void test_kraepelin_transform__cleanup(void) {}

void test_kraepelin_transform__window_matches_reference_for_full_and_partial_epochs(void) {
  static const uint16_t s_widths[] = {1, 11, 63, 124, 125};

  for (uint16_t width_index = 0; width_index < sizeof(s_widths) / sizeof(s_widths[0]);
       width_index++) {
    int16_t expected[KRAEPELIN_TRANSFORM_WIDTH] = {0};
    int16_t actual[KRAEPELIN_TRANSFORM_WIDTH] = {0};
    const uint16_t width = s_widths[width_index];
    prv_fill_asymmetric(expected, width);
    memcpy(actual, expected, sizeof(actual));

    prv_reference_window(expected, width);
    kraepelin_transform_window(actual, width);

    prv_assert_equal(expected, actual, KRAEPELIN_TRANSFORM_WIDTH);
  }
}

void test_kraepelin_transform__window_matches_reference_at_input_extrema(void) {
  int16_t expected[KRAEPELIN_TRANSFORM_WIDTH];
  int16_t actual[KRAEPELIN_TRANSFORM_WIDTH];
  for (uint16_t i = 0; i < KRAEPELIN_TRANSFORM_WIDTH; i++) {
    expected[i] = (i % 3 == 0) ? INT16_MIN : ((i % 3 == 1) ? INT16_MAX : -1);
  }
  memcpy(actual, expected, sizeof(actual));

  prv_reference_window(expected, 125);
  kraepelin_transform_window(actual, 125);

  prv_assert_equal(expected, actual, KRAEPELIN_TRANSFORM_WIDTH);
}

void test_kraepelin_transform__fft_coefficients_and_magnitudes_match_reference(void) {
  for (uint16_t signal = 0; signal < 64; signal++) {
    int16_t expected[KRAEPELIN_TRANSFORM_WIDTH];
    int16_t actual[KRAEPELIN_TRANSFORM_WIDTH];
    prv_fill_asymmetric(expected, KRAEPELIN_TRANSFORM_WIDTH);
    memcpy(actual, expected, sizeof(actual));

    fft_2radix_real(expected, 7);
    kraepelin_transform_fft(actual);
    prv_assert_equal(expected, actual, KRAEPELIN_TRANSFORM_WIDTH);

    fft_mag(expected, 7);
    kraepelin_transform_magnitudes(actual);
    prv_assert_equal(expected, actual, KRAEPELIN_TRANSFORM_MAGNITUDES);
  }
}

void test_kraepelin_transform__complete_epoch_matches_reference(void) {
  static const uint16_t s_widths[] = {1, 11, 63, 124, 125};

  for (uint16_t signal = 0; signal < 32; signal++) {
    int16_t expected[KRAEPELIN_TRANSFORM_AXES][KRAEPELIN_TRANSFORM_WIDTH] = {0};
    int16_t actual[KRAEPELIN_TRANSFORM_AXES][KRAEPELIN_TRANSFORM_WIDTH] = {0};
    int16_t expected_magnitudes[KRAEPELIN_TRANSFORM_MAGNITUDES];
    int16_t actual_magnitudes[KRAEPELIN_TRANSFORM_MAGNITUDES];
    const uint16_t width = s_widths[signal % (sizeof(s_widths) / sizeof(s_widths[0]))];

    for (uint16_t axis = 0; axis < KRAEPELIN_TRANSFORM_AXES; axis++) {
      prv_fill_asymmetric(expected[axis], width);
    }
    memcpy(actual, expected, sizeof(actual));

    prv_reference_epoch(expected, width, expected_magnitudes);
    kraepelin_transform_epoch(actual, width, actual_magnitudes);

    prv_assert_equal(&expected[0][0], &actual[0][0],
                     KRAEPELIN_TRANSFORM_AXES * KRAEPELIN_TRANSFORM_WIDTH);
    prv_assert_equal(expected_magnitudes, actual_magnitudes, KRAEPELIN_TRANSFORM_MAGNITUDES);
  }
}

void test_kraepelin_transform__complete_epoch_matches_reference_at_input_extrema(void) {
  int16_t expected[KRAEPELIN_TRANSFORM_AXES][KRAEPELIN_TRANSFORM_WIDTH] = {0};
  int16_t actual[KRAEPELIN_TRANSFORM_AXES][KRAEPELIN_TRANSFORM_WIDTH] = {0};
  int16_t expected_magnitudes[KRAEPELIN_TRANSFORM_MAGNITUDES];
  int16_t actual_magnitudes[KRAEPELIN_TRANSFORM_MAGNITUDES];

  for (uint16_t axis = 0; axis < KRAEPELIN_TRANSFORM_AXES; axis++) {
    for (uint16_t i = 0; i < 125; i++) {
      expected[axis][i] = ((axis + i) % 3 == 0) ? INT16_MIN : INT16_MAX;
    }
  }
  memcpy(actual, expected, sizeof(actual));

  prv_reference_epoch(expected, 125, expected_magnitudes);
  kraepelin_transform_epoch(actual, 125, actual_magnitudes);

  prv_assert_equal(&expected[0][0], &actual[0][0],
                   KRAEPELIN_TRANSFORM_AXES * KRAEPELIN_TRANSFORM_WIDTH);
  prv_assert_equal(expected_magnitudes, actual_magnitudes, KRAEPELIN_TRANSFORM_MAGNITUDES);
}
