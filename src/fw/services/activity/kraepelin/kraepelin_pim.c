/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "kraepelin_pim.h"

#include <stdlib.h>

int32_t kraepelin_pim_sum(const int16_t *samples, int num_samples,
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

  int32_t pim = 0;
  for (int i = 0; i < num_samples; i++) {
    Fixed_S64_32 output =
        math_fixed_recursive_filter(FIXED_S64_32_FROM_INT(samples[i]),
                                    KRAEPELIN_PIM_INPUT_STATE_SIZE, KRAEPELIN_PIM_OUTPUT_STATE_SIZE,
                                    s_input_coefficients, s_output_coefficients, state_x, state_y);
    pim += abs(FIXED_S64_32_TO_INT(output));
  }

  return pim;
}
