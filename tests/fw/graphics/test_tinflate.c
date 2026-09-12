/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "applib/vendor/tinflate/tinflate.h"

#include "pbl/util/size.h"

#include "clar.h"
#include "fake_pbl_malloc.h"

#include <stdint.h>
#include <string.h>

static const uint8_t s_fixed[] = {
    0x8b, 0x88, 0x8c, 0x18, 0xd4, 0x30, 0x31, 0x29, 0x39, 0x25, 0x35, 0x2d,
    0x3d, 0x23, 0x33, 0x2b, 0x3b, 0x27, 0x37, 0x2f, 0x7f, 0x48, 0x70, 0x01,
};

static const uint8_t s_dynamic[] = {
    0xed, 0xca, 0xb7, 0x01, 0x80, 0x30, 0x10, 0x04, 0xc1, 0x9c, 0x2a, 0xae, 0x35, 0x8c,
    0xf0, 0xe8, 0x41, 0xbc, 0x70, 0xd5, 0x43, 0x1f, 0x6c, 0x3c, 0xe3, 0x7d, 0xd0, 0x96,
    0x87, 0x7a, 0x52, 0x95, 0xec, 0x8c, 0x6a, 0xed, 0xd2, 0x98, 0x97, 0x75, 0x97, 0x1d,
    0x21, 0xc9, 0x3f, 0x9e, 0xcb, 0xe7, 0x56, 0x63, 0x5d, 0xe1, 0x5c, 0x2e, 0x97, 0xcb,
    0xe5, 0x72, 0xb9, 0x5c, 0x2e, 0x97, 0xcb, 0xe5, 0x72, 0xb9, 0x3f, 0xb8, 0x2f,
};

static const uint8_t s_stored[] = {
    0x01, 0x10, 0x00, 0xef, 0xff, 0x07, 0x24, 0x41, 0x5e, 0x7b, 0x98,
    0xb5, 0xd2, 0xef, 0x0c, 0x29, 0x46, 0x63, 0x80, 0x9d, 0xba,
};

static void prv_assert_decode(const uint8_t *compressed, size_t compressed_size,
                              const uint8_t *expected, size_t expected_size) {
  uint8_t output[4402];
  memset(output, 0xa5, sizeof(output));
  unsigned int output_size = expected_size;

  cl_assert_equal_i(TINF_OK,
                    tinflate_uncompress(output + 1, &output_size, compressed, compressed_size));
  cl_assert_equal_i(expected_size, output_size);
  cl_assert_equal_m(expected, output + 1, expected_size);
  cl_assert_equal_i(0xa5, output[0]);
  cl_assert_equal_i(0xa5, output[expected_size + 1]);
}

void test_tinflate__decodes_fixed_huffman_and_overlapping_matches(void) {
  uint8_t expected[340];
  for (size_t i = 0; i < 160; ++i) {
    expected[i] = (i & 1) ? 'Y' : 'X';
  }
  for (size_t i = 160; i < sizeof(expected); ++i) {
    expected[i] = 'a' + (i - 160) % 15;
  }

  prv_assert_decode(s_fixed, sizeof(s_fixed), expected, sizeof(expected));
}

void test_tinflate__decodes_dynamic_huffman(void) {
  static const char s_line[] = "the quick brown fox jumps over the lazy dog\n";
  uint8_t expected[4400];
  for (size_t i = 0; i < sizeof(expected); ++i) {
    expected[i] = s_line[i % (sizeof(s_line) - 1)];
  }

  prv_assert_decode(s_dynamic, sizeof(s_dynamic), expected, sizeof(expected));
}

void test_tinflate__decodes_stored_block(void) {
  uint8_t expected[16];
  for (size_t i = 0; i < sizeof(expected); ++i) {
    expected[i] = 7 + 29 * i;
  }

  prv_assert_decode(s_stored, sizeof(s_stored), expected, sizeof(expected));
}

void test_tinflate__accepts_empty_stream_and_trailing_data(void) {
  const uint8_t compressed[] = {0x03, 0x00, 0xa5, 0x5a};
  unsigned int output_size = 0;

  cl_assert_equal_i(TINF_OK,
                    tinflate_uncompress(NULL, &output_size, compressed, sizeof(compressed)));
  cl_assert_equal_i(0, output_size);
}

void test_tinflate__preserves_capacity_on_allocation_failure(void) {
  uint8_t output[4400];
  unsigned int output_size = sizeof(output);
  fake_malloc_set_largest_free_block(1);

  const int result = tinflate_uncompress(output, &output_size, s_dynamic, sizeof(s_dynamic));
  fake_malloc_set_largest_free_block(SIZE_MAX);

  cl_assert_equal_i(TINF_MEMORY_ERROR, result);
  cl_assert_equal_i(sizeof(output), output_size);
}

void test_tinflate__rejects_every_truncated_dynamic_prefix(void) {
#ifndef CONFIG_TINFLATE_RUST
  return;
#else
  uint8_t output[4400];
  for (size_t size = 0; size < sizeof(s_dynamic); ++size) {
    unsigned int output_size = sizeof(output);
    cl_assert(tinflate_uncompress(output, &output_size, s_dynamic, size) < 0);
    cl_assert(output_size <= sizeof(output));
  }
#endif
}

void test_tinflate__reports_destination_overflow_without_overwriting_canary(void) {
#ifndef CONFIG_TINFLATE_RUST
  return;
#else
  uint8_t output[341];
  memset(output, 0xa5, sizeof(output));
  unsigned int output_size = 339;

  cl_assert_equal_i(TINF_DEST_OVERFLOW,
                    tinflate_uncompress(output + 1, &output_size, s_fixed, sizeof(s_fixed)));
  cl_assert(output_size <= 339);
  cl_assert_equal_i(0xa5, output[0]);
  cl_assert_equal_i(0xa5, output[340]);
#endif
}

void test_tinflate__rejects_null_and_empty_input(void) {
#ifndef CONFIG_TINFLATE_RUST
  return;
#else
  uint8_t output;
  unsigned int output_size = 1;

  cl_assert_equal_i(TINF_DATA_ERROR, tinflate_uncompress(&output, &output_size, NULL, 0));
  cl_assert_equal_i(0, output_size);
#endif
}
