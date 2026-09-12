/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "kraepelin_transform.h"

#include "pbl/util/trig.h"

static int32_t prv_mean(const int16_t *samples, uint16_t num_samples) {
  int32_t mean = 0;

  for (uint16_t i = 0; i < num_samples; i++) {
    mean += samples[i];
  }
  return mean / num_samples;
}

static uint32_t prv_isqrt(uint32_t value) {
  uint32_t remainder = value;
  uint32_t result = 0;
  uint32_t bit = 1 << 30;

  while (bit > remainder) {
    bit >>= 2;
  }
  while (bit != 0) {
    if (remainder >= result + bit) {
      remainder -= result + bit;
      result += bit << 1;
    }
    result >>= 1;
    bit >>= 2;
  }
  return result;
}

void kraepelin_transform_window(int16_t samples[KRAEPELIN_TRANSFORM_WIDTH], uint16_t num_samples) {
  const int32_t mean = prv_mean(samples, num_samples);

  for (uint16_t i = 0; i < num_samples; i++) {
    samples[i] =
        (int16_t)(((samples[i] - mean) * sin_lookup((TRIG_MAX_ANGLE * i) / (2 * num_samples))) /
                  TRIG_MAX_RATIO);
  }
}

void kraepelin_transform_fft(int16_t samples[KRAEPELIN_TRANSFORM_WIDTH]) {
  int16_t j = 1;

  for (int16_t i = 1; i < KRAEPELIN_TRANSFORM_WIDTH; i++) {
    if (i < j) {
      const int16_t value = samples[j - 1];
      samples[j - 1] = samples[i - 1];
      samples[i - 1] = value;
    }
    int16_t k = KRAEPELIN_TRANSFORM_WIDTH / 2;
    while (k < j) {
      j -= k;
      k /= 2;
    }
    j += k;
  }

  for (int16_t i = 1; i <= KRAEPELIN_TRANSFORM_WIDTH; i += 2) {
    const int16_t value = samples[i - 1];
    samples[i - 1] = value + samples[i];
    samples[i] = value - samples[i];
  }

  int16_t n2 = 1;
  for (int16_t stage = 2; stage <= 7; stage++) {
    const int16_t n4 = n2;
    n2 = 2 * n4;
    const int16_t n1 = 2 * n2;
    const int32_t angle_step = TRIG_MAX_ANGLE / n1;

    for (int16_t i = 1; i <= KRAEPELIN_TRANSFORM_WIDTH; i += n1) {
      const int16_t value = samples[i - 1];
      samples[i - 1] = value + samples[i + n2 - 1];
      samples[i + n2 - 1] = value - samples[i + n2 - 1];
      samples[i + n4 + n2 - 1] = -samples[i + n4 + n2 - 1];
      int32_t angle = angle_step;

      for (int16_t offset = 1; offset < n4; offset++) {
        const int16_t i1 = i + offset;
        const int16_t i2 = i - offset + n2;
        const int16_t i3 = i + offset + n2;
        const int16_t i4 = i - offset + n1;
        const int32_t sine = sin_lookup(angle);
        const int32_t cosine = cos_lookup(angle);
        angle += angle_step;

        const int16_t t1 =
            (int16_t)((samples[i3 - 1] * cosine + samples[i4 - 1] * sine) / TRIG_MAX_ANGLE);
        const int16_t t2 =
            (int16_t)((samples[i3 - 1] * sine - samples[i4 - 1] * cosine) / TRIG_MAX_ANGLE);
        samples[i4 - 1] = samples[i2 - 1] - t2;
        samples[i3 - 1] = -samples[i2 - 1] - t2;
        samples[i2 - 1] = samples[i1 - 1] - t1;
        samples[i1 - 1] += t1;
      }
    }
  }
}

void kraepelin_transform_magnitudes(int16_t samples[KRAEPELIN_TRANSFORM_WIDTH]) {
  for (uint16_t i = 1; i < KRAEPELIN_TRANSFORM_MAGNITUDES; i++) {
    samples[i] = prv_isqrt(samples[i] * samples[i] + samples[KRAEPELIN_TRANSFORM_WIDTH - i] *
                                                         samples[KRAEPELIN_TRANSFORM_WIDTH - i]);
  }
}

void kraepelin_transform_epoch(int16_t samples[KRAEPELIN_TRANSFORM_AXES][KRAEPELIN_TRANSFORM_WIDTH],
                               uint16_t num_samples,
                               int16_t magnitudes[KRAEPELIN_TRANSFORM_MAGNITUDES]) {
  for (uint16_t axis = 0; axis < KRAEPELIN_TRANSFORM_AXES; axis++) {
    kraepelin_transform_window(samples[axis], num_samples);

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

    kraepelin_transform_fft(samples[axis]);
    kraepelin_transform_magnitudes(samples[axis]);
  }

  for (uint16_t i = 0; i < KRAEPELIN_TRANSFORM_MAGNITUDES; i++) {
    magnitudes[i] =
        (int16_t)prv_isqrt(samples[0][i] * samples[0][i] + samples[1][i] * samples[1][i] +
                           samples[2][i] * samples[2][i]);
  }
}
