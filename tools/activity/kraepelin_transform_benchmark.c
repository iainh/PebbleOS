/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "services/activity/kraepelin/kraepelin_transform.h"

void kraepelin_transform_c_window(int16_t *samples, uint16_t num_samples);
void kraepelin_transform_c_fft(int16_t *samples);
void kraepelin_transform_c_epoch(
    int16_t samples[KRAEPELIN_TRANSFORM_AXES][KRAEPELIN_TRANSFORM_WIDTH], uint16_t num_samples,
    int16_t *magnitudes);

typedef void (*WindowFunction)(int16_t *, uint16_t);
typedef void (*FftFunction)(int16_t *);
typedef void (*EpochFunction)(int16_t[KRAEPELIN_TRANSFORM_AXES][KRAEPELIN_TRANSFORM_WIDTH],
                              uint16_t, int16_t *);

static int16_t s_axis_seed[KRAEPELIN_TRANSFORM_WIDTH];
static int16_t s_epoch_seed[KRAEPELIN_TRANSFORM_AXES][KRAEPELIN_TRANSFORM_WIDTH];
static volatile uint32_t s_checksum;

static uint64_t prv_now_ns(void) {
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC_RAW, &time);
  return (uint64_t)time.tv_sec * 1000000000 + time.tv_nsec;
}

static double prv_benchmark_copy(size_t size, uint32_t iterations) {
  int16_t destination[KRAEPELIN_TRANSFORM_AXES][KRAEPELIN_TRANSFORM_WIDTH];
  const uint64_t start = prv_now_ns();
  for (uint32_t i = 0; i < iterations; i++) {
    memcpy(destination, s_epoch_seed, size);
    s_checksum += destination[0][i & (KRAEPELIN_TRANSFORM_WIDTH - 1)];
  }
  return (double)(prv_now_ns() - start) / iterations;
}

static double prv_benchmark_window(WindowFunction function, uint32_t iterations) {
  int16_t samples[KRAEPELIN_TRANSFORM_WIDTH];
  const uint64_t start = prv_now_ns();
  for (uint32_t i = 0; i < iterations; i++) {
    memcpy(samples, s_axis_seed, sizeof(samples));
    function(samples, 125);
    s_checksum += samples[i & (KRAEPELIN_TRANSFORM_WIDTH - 1)];
  }
  return (double)(prv_now_ns() - start) / iterations;
}

static double prv_benchmark_fft(FftFunction function, uint32_t iterations) {
  int16_t samples[KRAEPELIN_TRANSFORM_WIDTH];
  const uint64_t start = prv_now_ns();
  for (uint32_t i = 0; i < iterations; i++) {
    memcpy(samples, s_axis_seed, sizeof(samples));
    function(samples);
    s_checksum += samples[i & (KRAEPELIN_TRANSFORM_WIDTH - 1)];
  }
  return (double)(prv_now_ns() - start) / iterations;
}

static double prv_benchmark_epoch(EpochFunction function, uint32_t iterations) {
  int16_t samples[KRAEPELIN_TRANSFORM_AXES][KRAEPELIN_TRANSFORM_WIDTH];
  int16_t magnitudes[KRAEPELIN_TRANSFORM_MAGNITUDES];
  const uint64_t start = prv_now_ns();
  for (uint32_t i = 0; i < iterations; i++) {
    memcpy(samples, s_epoch_seed, sizeof(samples));
    function(samples, 125, magnitudes);
    s_checksum += magnitudes[i & (KRAEPELIN_TRANSFORM_MAGNITUDES - 1)];
  }
  return (double)(prv_now_ns() - start) / iterations;
}

static void prv_report(const char *name, double copy_ns, double c_ns, double rust_ns) {
  c_ns -= copy_ns;
  rust_ns -= copy_ns;
  printf("%-7s C %9.1f ns  Rust %9.1f ns  speedup %5.2fx  reduction %5.1f%%\n", name, c_ns, rust_ns,
         c_ns / rust_ns, 100.0 * (c_ns - rust_ns) / c_ns);
}

int main(void) {
  uint32_t random = 0x51f15eed;
  for (uint16_t axis = 0; axis < KRAEPELIN_TRANSFORM_AXES; axis++) {
    for (uint16_t i = 0; i < KRAEPELIN_TRANSFORM_WIDTH; i++) {
      random = random * 1664525 + 1013904223;
      s_epoch_seed[axis][i] = (int16_t)((random >> 16) % 2001) - 1000 + 3 * i - 127;
    }
  }
  memcpy(s_axis_seed, s_epoch_seed[0], sizeof(s_axis_seed));

  const uint32_t iterations = 100000;
  const double axis_copy = prv_benchmark_copy(sizeof(s_axis_seed), iterations);
  const double epoch_copy = prv_benchmark_copy(sizeof(s_epoch_seed), iterations);
  prv_report("window", axis_copy, prv_benchmark_window(kraepelin_transform_c_window, iterations),
             prv_benchmark_window(kraepelin_transform_window, iterations));
  prv_report("fft", axis_copy, prv_benchmark_fft(kraepelin_transform_c_fft, iterations),
             prv_benchmark_fft(kraepelin_transform_fft, iterations));
  prv_report("epoch", epoch_copy, prv_benchmark_epoch(kraepelin_transform_c_epoch, iterations),
             prv_benchmark_epoch(kraepelin_transform_epoch, iterations));
  printf("checksum %u\n", s_checksum);
  return 0;
}
