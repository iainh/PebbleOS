/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "util/base64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clar.h"

#include "stubs_passert.h"

// Stubs
///////////////////////////////////////////////////////////
int g_pbl_log_level = 0;
void pbl_log(const char *src_filename, int src_line_number, const char *fmt, ...) {}

// Tests
///////////////////////////////////////////////////////////
static void prv_test_decode_encode(const char *test_name, char *buffer, unsigned int buffer_length,
                                   const uint8_t *expected, unsigned int expected_length) {
  cl_assert(buffer_length % 4 == 0);

  char original_in[buffer_length + 1];
  memcpy(original_in, buffer, buffer_length + 1);

  // test decode
  unsigned int num_bytes = base64_decode_inplace(buffer, buffer_length);
  cl_assert(num_bytes == expected_length);
  cl_assert(memcmp(buffer, expected, num_bytes) == 0);

  // test encode
  char out[buffer_length + 1];
  int result = base64_encode(out, buffer_length + 1, expected, expected_length);
  cl_assert_equal_i(result, buffer_length);
  cl_assert_equal_m(out, original_in, buffer_length);
}

void test_base64__initialize(void) {}

void test_base64__cleanup(void) {}

void test_base64__decode(void) {
  {
    char buffer[] = "";
    const uint8_t expected[] = {0};
    prv_test_decode_encode("empty", buffer, 0, expected, 0);
  }

  {
    char buffer[] = "Zg==";
    const uint8_t expected[] = {'f'};
    prv_test_decode_encode("one byte", buffer, 4, expected, 1);
  }

  {
    char buffer[] = "Zm8=";
    const uint8_t expected[] = {'f', 'o'};
    prv_test_decode_encode("two bytes", buffer, 4, expected, 2);
  }

  {
    char buffer[] = "Zm9v";
    const uint8_t expected[] = {'f', 'o', 'o'};
    prv_test_decode_encode("three bytes", buffer, 4, expected, 3);
  }

  {
    char buffer[] = "+///";
    const uint8_t expected[] = {0xfb, 0xff, 0xff};
    prv_test_decode_encode("symbols", buffer, 4, expected, 3);
  }

  {
    char buffer[] = "abcd";
    const uint8_t expected[] = {0x69, 0xb7, 0x1d};
    prv_test_decode_encode("basic", buffer, 4, expected, 3);
  }

  {
    char buffer[] = "ABCD";
    const uint8_t expected[] = {0x0, 0x10, 0x83};
    prv_test_decode_encode("upper", buffer, 4, expected, 3);
  }

  {
    char buffer[] = "abcdABCD";
    const uint8_t expected[] = {0x69, 0xb7, 0x1d, 0x0, 0x10, 0x83};
    prv_test_decode_encode("twobyte", buffer, 8, expected, 6);
  }

  {
    char buffer[] = "vu8=";
    const uint8_t expected[] = {0xbe, 0xef};
    prv_test_decode_encode("1pad", buffer, 4, expected, 2);
  }

  {
    char buffer[] = "aQ==";
    const uint8_t expected[] = {0x69};
    prv_test_decode_encode("2pad", buffer, 4, expected, 1);
  }
}

void test_base64__decode_rejects_invalid_input(void) {
  char incomplete[] = "abc";
  cl_assert_equal_i(base64_decode_inplace(incomplete, sizeof(incomplete) - 1), 0);

  char invalid_character[] = "ab?d";
  cl_assert_equal_i(base64_decode_inplace(invalid_character, sizeof(invalid_character) - 1), 0);

  char misplaced_padding[] = "ab=c";
  cl_assert_equal_i(base64_decode_inplace(misplaced_padding, sizeof(misplaced_padding) - 1), 0);

  char padding_in_middle[] = "aQ==aQ==";
  cl_assert_equal_i(base64_decode_inplace(padding_in_middle, sizeof(padding_in_middle) - 1), 0);

  char whitespace[] = "a Q=";
  cl_assert_equal_i(base64_decode_inplace(whitespace, sizeof(whitespace) - 1), 0);
}

void test_base64__decode_accepts_noncanonical_pad_bits(void) {
  char buffer[] = "Zh==";

  cl_assert_equal_i(base64_decode_inplace(buffer, sizeof(buffer) - 1), 1);
  cl_assert_equal_i(buffer[0], 'f');
}

void test_base64__encode_reports_required_space_without_writing(void) {
  const uint8_t input[] = {0xbe, 0xef};
  char output[] = "keep";

  cl_assert_equal_i(base64_encode(output, 3, input, sizeof(input)), 4);
  cl_assert_equal_s(output, "keep");
}

void test_base64__encode_null_termination(void) {
  const uint8_t input[] = {0x69};
  char exact[4];
  char terminated[5];

  memset(exact, 'x', sizeof(exact));
  memset(terminated, 'x', sizeof(terminated));
  cl_assert_equal_i(base64_encode(exact, sizeof(exact), input, sizeof(input)), 4);
  cl_assert_equal_m(exact, "aQ==", sizeof(exact));
  cl_assert_equal_i(base64_encode(terminated, sizeof(terminated), input, sizeof(input)), 4);
  cl_assert_equal_s(terminated, "aQ==");
}
