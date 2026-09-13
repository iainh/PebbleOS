/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_PFS_LOOKUP_RUST

// Four entries occupy 32 bytes. PFS serializes access with its mutex.
uint32_t pfs_lookup_cache_hash(const uint8_t *name, size_t length);
void pfs_lookup_cache_reset(void);
bool pfs_lookup_cache_get(uint32_t hash, uint8_t length, uint16_t *page);
void pfs_lookup_cache_put(uint32_t hash, uint8_t length, uint16_t page);
#endif

#if defined(CONFIG_PERFORMANCE_TESTS) || UNITTEST
typedef struct {
  uint32_t elapsed_ticks;
  uint32_t scanned_pages;
  uint32_t name_reads;
  uint32_t cache_candidates;
  uint32_t cache_hits;
  uint32_t fallbacks;
  uint32_t integrity;
} PFSLookupBenchmark;

// Counters cover locate_flash_file calls after reset. Integrity is a stable
// digest of each result and page, suitable for comparing benchmark variants.
void pfs_lookup_benchmark_reset(void);
void pfs_lookup_benchmark_get(PFSLookupBenchmark *result);
#endif

#ifdef CONFIG_PERFORMANCE_TESTS
bool pfs_lookup_benchmark_prepare(void);
uint32_t pfs_lookup_benchmark_run(uint32_t iterations);
#endif
