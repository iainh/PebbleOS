/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stddef.h>

void *test_applib_malloc(size_t size);
void *test_applib_zalloc(size_t size);
void test_applib_free(void *ptr);

#define applib_type_zalloc(Type) test_applib_zalloc(sizeof(Type))
#define applib_type_malloc(Type) test_applib_malloc(sizeof(Type))
#define applib_type_size(Type) sizeof(Type)
#define applib_malloc(size) test_applib_malloc(size)
#define applib_free(ptr) test_applib_free(ptr)
