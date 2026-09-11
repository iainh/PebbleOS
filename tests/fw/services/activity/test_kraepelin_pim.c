/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "services/activity/kraepelin/kraepelin_pim.h"

#include "pbl/util/size.h"

#include "clar.h"

#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(Fixed_S64_32) == 8, "Fixed_S64_32 ABI changed");
_Static_assert(_Alignof(Fixed_S64_32) == 1, "Fixed_S64_32 alignment changed");

static int32_t prv_reference_sum(const int16_t *samples, int num_samples,
                                 Fixed_S64_32 state_x[KRAEPELIN_PIM_INPUT_STATE_SIZE],
                                 Fixed_S64_32 state_y[KRAEPELIN_PIM_OUTPUT_STATE_SIZE]) {
  static const Fixed_S64_32 s_input_coefficients[KRAEPELIN_PIM_INPUT_STATE_SIZE] = {
      {0x000000000721d150LL}, {0x0000000000000000LL}, {0xfffffffff1bc5d60LL},
      {0x0000000000000000LL}, {0x000000000721d150LL},
  };
  static const Fixed_S64_32 s_output_coefficients[KRAEPELIN_PIM_OUTPUT_STATE_SIZE] = {
      {0xfffffffc92b0910cLL},
      {0x0000000473f9a693LL},
      {0xfffffffd633c7d23LL},
      {0x0000000096405b5cLL},
  };

  int32_t sum = 0;
  for (int i = 0; i < num_samples; ++i) {
    const Fixed_S64_32 output =
        math_fixed_recursive_filter(FIXED_S64_32_FROM_INT(samples[i]),
                                    KRAEPELIN_PIM_INPUT_STATE_SIZE, KRAEPELIN_PIM_OUTPUT_STATE_SIZE,
                                    s_input_coefficients, s_output_coefficients, state_x, state_y);
    sum += abs(FIXED_S64_32_TO_INT(output));
  }
  return sum;
}

static void prv_assert_matches_reference(const int16_t *samples, int num_samples,
                                         Fixed_S64_32 actual_x[], Fixed_S64_32 actual_y[],
                                         Fixed_S64_32 expected_x[], Fixed_S64_32 expected_y[]) {
  const int32_t expected = prv_reference_sum(samples, num_samples, expected_x, expected_y);
  const int32_t actual = kraepelin_pim_sum(samples, num_samples, actual_x, actual_y);

  cl_assert_equal_i(expected, actual);
  cl_assert_equal_m(expected_x, actual_x, sizeof(Fixed_S64_32) * KRAEPELIN_PIM_INPUT_STATE_SIZE);
  cl_assert_equal_m(expected_y, actual_y, sizeof(Fixed_S64_32) * KRAEPELIN_PIM_OUTPUT_STATE_SIZE);
}

void test_kraepelin_pim__matches_reference_across_blocks(void) {
  const int16_t samples[] = {32767, -32768, 32767, -91,  203,  -17, 0,    1,    -1,
                             4096,  -2048,  777,   -333, 8191, 42,  -999, 1234, -55,
                             8,     16000,  -7,    88,   512,  -12, 300};
  Fixed_S64_32 actual_x[KRAEPELIN_PIM_INPUT_STATE_SIZE] = {0};
  Fixed_S64_32 actual_y[KRAEPELIN_PIM_OUTPUT_STATE_SIZE] = {0};
  Fixed_S64_32 expected_x[KRAEPELIN_PIM_INPUT_STATE_SIZE] = {0};
  Fixed_S64_32 expected_y[KRAEPELIN_PIM_OUTPUT_STATE_SIZE] = {0};

  prv_assert_matches_reference(samples, 10, actual_x, actual_y, expected_x, expected_y);
  prv_assert_matches_reference(samples + 10, 15, actual_x, actual_y, expected_x, expected_y);
}

void test_kraepelin_pim__matches_reference_with_fractional_history(void) {
  const int16_t samples[] = {13, -29, 7};
  Fixed_S64_32 actual_x[KRAEPELIN_PIM_INPUT_STATE_SIZE] = {
      FIXED_S64_32_FROM_INT(11), FIXED_S64_32_FROM_INT(-7), FIXED_S64_32_FROM_INT(5),
      FIXED_S64_32_FROM_INT(19), FIXED_S64_32_FROM_INT(-23)};
  Fixed_S64_32 actual_y[KRAEPELIN_PIM_OUTPUT_STATE_SIZE] = {
      FIXED_S64_32_FROM_RAW(1), FIXED_S64_32_FROM_RAW(-1), FIXED_S64_32_FROM_RAW(0xffffffff),
      FIXED_S64_32_FROM_RAW(0x100000001)};
  Fixed_S64_32 expected_x[KRAEPELIN_PIM_INPUT_STATE_SIZE];
  Fixed_S64_32 expected_y[KRAEPELIN_PIM_OUTPUT_STATE_SIZE];
  memcpy(expected_x, actual_x, sizeof(expected_x));
  memcpy(expected_y, actual_y, sizeof(expected_y));

  prv_assert_matches_reference(samples, 3, actual_x, actual_y, expected_x, expected_y);
}

void test_kraepelin_pim__matches_reference_for_long_nonperiodic_input(void) {
  Fixed_S64_32 actual_x[KRAEPELIN_PIM_INPUT_STATE_SIZE] = {0};
  Fixed_S64_32 actual_y[KRAEPELIN_PIM_OUTPUT_STATE_SIZE] = {0};
  Fixed_S64_32 expected_x[KRAEPELIN_PIM_INPUT_STATE_SIZE] = {0};
  Fixed_S64_32 expected_y[KRAEPELIN_PIM_OUTPUT_STATE_SIZE] = {0};
  uint32_t random_state = 0x4b1d5eed;

  for (int block = 0; block < 100; ++block) {
    int16_t samples[25];
    for (size_t i = 0; i < ARRAY_LENGTH(samples); ++i) {
      random_state = random_state * 1664525 + 1013904223;
      samples[i] = (int16_t)((random_state >> 20) - 2048);
    }
    prv_assert_matches_reference(samples, ARRAY_LENGTH(samples), actual_x, actual_y, expected_x,
                                 expected_y);
  }
}

void test_kraepelin_pim__zero_samples_preserves_state(void) {
  Fixed_S64_32 state_x[KRAEPELIN_PIM_INPUT_STATE_SIZE] = {
      FIXED_S64_32_FROM_INT(1), FIXED_S64_32_FROM_INT(2), FIXED_S64_32_FROM_INT(3),
      FIXED_S64_32_FROM_INT(4), FIXED_S64_32_FROM_INT(5)};
  Fixed_S64_32 state_y[KRAEPELIN_PIM_OUTPUT_STATE_SIZE] = {
      FIXED_S64_32_FROM_RAW(1), FIXED_S64_32_FROM_RAW(-1), FIXED_S64_32_FROM_RAW(0xffffffff),
      FIXED_S64_32_FROM_RAW(0x100000001)};
  Fixed_S64_32 expected_x[KRAEPELIN_PIM_INPUT_STATE_SIZE];
  Fixed_S64_32 expected_y[KRAEPELIN_PIM_OUTPUT_STATE_SIZE];
  memcpy(expected_x, state_x, sizeof(expected_x));
  memcpy(expected_y, state_y, sizeof(expected_y));

  cl_assert_equal_i(0, kraepelin_pim_sum(NULL, 0, state_x, state_y));
  cl_assert_equal_m(expected_x, state_x, sizeof(expected_x));
  cl_assert_equal_m(expected_y, state_y, sizeof(expected_y));
}
