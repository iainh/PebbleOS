/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "clar.h"

#include "pbl/util/crc32.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define assert_equal_hex(A, B) \
  do { \
    uint32_t a = (A); \
    uint32_t b = (B); \
    if (a != b) { \
      char error_msg[256]; \
      sprintf(error_msg, \
              "%#08"PRIx32" != %#08"PRIx32"\n", \
              a, b); \
      clar__assert(0, __FILE__, __LINE__, \
                   #A " != " #B, error_msg, 1); \
    } \
  } while (0)

static uint32_t crc;

static uint32_t prv_crc32_bitwise(uint32_t value, const uint8_t *data, size_t length) {
  value ^= UINT32_MAX;
  while (length--) {
    value ^= *data++;
    for (unsigned int bit = 0; bit < 8; ++bit) {
      value = (value >> 1) ^ ((0u - (value & 1)) & 0xedb88320);
    }
  }
  return value ^ UINT32_MAX;
}

void test_crc32__initialize(void) {
  crc = crc32(0, NULL, 0);
}

void test_crc32__initial_value_matches_header(void) {
  cl_assert_equal_i(crc, CRC32_INIT);
}

void test_crc32__null(void) {
  cl_assert_equal_i(crc, 0);
}

void test_crc32__null_ignores_crc_and_length(void) {
  assert_equal_hex(crc32(0x12345678, NULL, 17), 0);
}

void test_crc32__empty_buffer(void) {
  crc = crc32(crc, "arbitrary pointer", 0);
  assert_equal_hex(crc, 0);
}

void test_crc32__one_byte(void) {
  crc = crc32(crc, "abcdefg", 1);
  assert_equal_hex(crc, 0xe8b7be43);
}

void test_crc32__standard_check(void) {
  // "Check" value from "A Painless Guide to CRC Error Detection Algorithms"
  crc = crc32(crc, "123456789", 9);
  assert_equal_hex(crc, 0xCBF43926);
}

void test_crc32__incremental(void) {
  crc = crc32(crc, "12", 2);
  crc = crc32(crc, "3456789", 7);
  assert_equal_hex(crc, 0xCBF43926);
}

void test_crc32__unaligned_and_tail_lengths(void) {
  uint8_t data[67];
  for (size_t i = 0; i < sizeof(data); ++i) {
    data[i] = (uint8_t)(i * 37 + 11);
  }

  for (size_t offset = 0; offset < 4; ++offset) {
    for (size_t length = 0; length <= 63; ++length) {
      const uint32_t initial = 0x12345678;
      assert_equal_hex(crc32(initial, data + offset, length),
                       prv_crc32_bitwise(initial, data + offset, length));
    }
  }
}

void test_crc32__residue(void) {
  uint8_t message[30];
  memcpy(message, "1234567890", 10);
  crc = crc32(crc, message, 10);

  message[10] = crc & 0xff;
  message[11] = (crc >> 8) & 0xff;
  message[12] = (crc >> 16) & 0xff;
  message[13] = (crc >> 24) & 0xff;
  assert_equal_hex(crc32(crc32(0, NULL, 0), message, 14), CRC32_RESIDUE);
}

void test_crc32__null_residue(void) {
  uint8_t data[4] = {0, 0, 0, 0};
  crc = crc32(crc, data, 4);
  assert_equal_hex(crc, CRC32_RESIDUE);
}
