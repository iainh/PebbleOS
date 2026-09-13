/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "pbl/services/filesystem/pfs.h"

#include "clar.h"
#include "fake_spi_flash.h"
#include "stubs_analytics.h"
#include "stubs_logging.h"
#include "stubs_mutex.h"
#include "stubs_passert.h"
#include "stubs_pbl_malloc.h"
#include "stubs_pebble_tasks.h"
#include "stubs_print.h"
#include "stubs_prompt.h"
#include "stubs_serial.h"
#include "stubs_sleep.h"
#include "stubs_task_watchdog.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#define SAMPLES 7

#ifndef PFS_BENCHMARK_IMPL
#define PFS_BENCHMARK_IMPL "unknown"
#endif

static uint8_t s_page[4096];
static uint8_t s_large[65536];
static volatile uint32_t s_checksum;

static uint64_t prv_now_ns(void) {
  struct timeval time;
  gettimeofday(&time, NULL);
  return (uint64_t)time.tv_sec * 1000000000ULL + (uint64_t)time.tv_usec * 1000;
}

static void prv_sort(uint64_t values[SAMPLES]) {
  for (int i = 1; i < SAMPLES; ++i) {
    uint64_t value = values[i];
    int j = i;
    while (j > 0 && values[j - 1] > value) {
      values[j] = values[j - 1];
      --j;
    }
    values[j] = value;
  }
}

typedef void (*BenchmarkFn)(int iterations);

static void prv_report(const char *name, int iterations, BenchmarkFn setup, BenchmarkFn run) {
  uint64_t elapsed[SAMPLES];
  uint32_t reads = 0;
  uint64_t read_bytes = 0;
  uint32_t writes = 0;
  uint64_t write_bytes = 0;
  uint32_t erases = 0;

  for (int sample = 0; sample < SAMPLES; ++sample) {
    pfs_format(false);
    setup(iterations);
    fake_flash_counters_reset();
    uint64_t begin = prv_now_ns();
    run(iterations);
    elapsed[sample] = (prv_now_ns() - begin) / iterations;
    if (sample == SAMPLES / 2) {
      reads = fake_flash_read_count();
      read_bytes = fake_flash_read_bytes();
      writes = fake_flash_write_count();
      write_bytes = fake_flash_write_bytes();
      erases = fake_flash_erase_count();
    }
  }
  prv_sort(elapsed);
  printf("pfs_benchmark,%s,%s,%d,%llu,%u,%llu,%u,%llu,%u,%u\n", PFS_BENCHMARK_IMPL, name,
         iterations, (unsigned long long)elapsed[SAMPLES / 2], reads,
         (unsigned long long)read_bytes, writes, (unsigned long long)write_bytes, erases,
         s_checksum);
}

static void prv_no_setup(int iterations) {
  (void)iterations;
}

static void prv_create_small(int iterations) {
  char name[24];
  for (int i = 0; i < iterations; ++i) {
    snprintf(name, sizeof(name), "small-%d", i);
    int fd = pfs_open(name, OP_FLAG_WRITE, FILE_TYPE_STATIC, 32);
    cl_assert(fd >= 0);
    cl_assert_equal_i(pfs_write(fd, s_page, 32), 32);
    cl_assert_equal_i(pfs_close(fd), S_SUCCESS);
    s_checksum += fd;
  }
}

static void prv_setup_page(int iterations) {
  (void)iterations;
  int fd = pfs_open("page", OP_FLAG_WRITE, FILE_TYPE_STATIC, sizeof(s_page));
  cl_assert(fd >= 0);
  cl_assert_equal_i(pfs_write(fd, s_page, sizeof(s_page)), sizeof(s_page));
  cl_assert_equal_i(pfs_close(fd), S_SUCCESS);
}

static void prv_read_page(int iterations) {
  for (int i = 0; i < iterations; ++i) {
    int fd = pfs_open("page", OP_FLAG_READ, 0, 0);
    cl_assert(fd >= 0);
    cl_assert_equal_i(pfs_read(fd, s_page, sizeof(s_page)), sizeof(s_page));
    cl_assert_equal_i(pfs_close(fd), S_SUCCESS);
    s_checksum += s_page[(i * 131) % sizeof(s_page)];
  }
}

static void prv_read_page_skip_crc(int iterations) {
  for (int i = 0; i < iterations; ++i) {
    int fd = pfs_open("page", OP_FLAG_READ | OP_FLAG_SKIP_HDR_CRC_CHECK, 0, 0);
    cl_assert(fd >= 0);
    cl_assert_equal_i(pfs_read(fd, s_page, sizeof(s_page)), sizeof(s_page));
    cl_assert_equal_i(pfs_close(fd), S_SUCCESS);
    s_checksum += s_page[(i * 131) % sizeof(s_page)];
  }
}

static void prv_overwrite_page(int iterations) {
  for (int i = 0; i < iterations; ++i) {
    s_page[0] = (uint8_t)i;
    int fd = pfs_open("page", OP_FLAG_OVERWRITE, FILE_TYPE_STATIC, sizeof(s_page));
    cl_assert(fd >= 0);
    cl_assert_equal_i(pfs_write(fd, s_page, sizeof(s_page)), sizeof(s_page));
    cl_assert_equal_i(pfs_close(fd), S_SUCCESS);
    s_checksum += fd;
  }
}

static void prv_setup_large(int iterations) {
  (void)iterations;
  int fd = pfs_open("large", OP_FLAG_WRITE, FILE_TYPE_STATIC, sizeof(s_large));
  cl_assert(fd >= 0);
  cl_assert_equal_i(pfs_write(fd, s_large, sizeof(s_large)), sizeof(s_large));
  cl_assert_equal_i(pfs_close(fd), S_SUCCESS);
}

static void prv_random_read(int iterations) {
  uint8_t value;
  int fd = pfs_open("large", OP_FLAG_READ | OP_FLAG_USE_PAGE_CACHE, 0, 0);
  cl_assert(fd >= 0);
  for (int i = 0; i < iterations; ++i) {
    uint32_t offset = (uint32_t)(i * 4051) % sizeof(s_large);
    cl_assert_equal_i(pfs_seek(fd, offset, FSeekSet), offset);
    cl_assert_equal_i(pfs_read(fd, &value, 1), 1);
    s_checksum += value;
  }
  cl_assert_equal_i(pfs_close(fd), S_SUCCESS);
}

static void prv_random_read_skip_crc(int iterations) {
  uint8_t value;
  int fd =
      pfs_open("large", OP_FLAG_READ | OP_FLAG_SKIP_HDR_CRC_CHECK | OP_FLAG_USE_PAGE_CACHE, 0, 0);
  cl_assert(fd >= 0);
  for (int i = 0; i < iterations; ++i) {
    uint32_t offset = (uint32_t)(i * 4051) % sizeof(s_large);
    cl_assert_equal_i(pfs_seek(fd, offset, FSeekSet), offset);
    cl_assert_equal_i(pfs_read(fd, &value, 1), 1);
    s_checksum += value;
  }
  cl_assert_equal_i(pfs_close(fd), S_SUCCESS);
}

void test_pfs_benchmark__initialize(void) {
  fake_spi_flash_init(0, 0x1000000);
  cl_assert_equal_i(pfs_init(false), S_SUCCESS);
  for (size_t i = 0; i < sizeof(s_large); ++i) {
    s_large[i] = (uint8_t)(i * 37 + 11);
  }
  memcpy(s_page, s_large, sizeof(s_page));
}

void test_pfs_benchmark__cleanup(void) {
  fake_spi_flash_cleanup();
}

void test_pfs_benchmark__run(void) {
  puts(
      "kind,implementation,workload,iterations,median_ns,read_calls,read_bytes,write_calls,write_"
      "bytes,erase_calls,checksum");
  prv_report("create_small", 16, prv_no_setup, prv_create_small);
  prv_report("read_page", 32, prv_setup_page, prv_read_page);
  prv_report("read_page_skip_crc", 32, prv_setup_page, prv_read_page_skip_crc);
  prv_report("overwrite_page", 8, prv_setup_page, prv_overwrite_page);
  prv_report("random_read", 128, prv_setup_large, prv_random_read);
  prv_report("random_read_skip_crc", 128, prv_setup_large, prv_random_read_skip_crc);
}
