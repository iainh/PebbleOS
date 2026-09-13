# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

file(GLOB_RECURSE sources CONFIGURE_DEPENDS *.c)

if(CONFIG_CRC32_RUST)
  list(REMOVE_ITEM sources ${CMAKE_CURRENT_SOURCE_DIR}/crc32.c)
  include(rust)
  pbl_rust_object(pebble_crc32 ${CMAKE_CURRENT_SOURCE_DIR}/crc32-rust rust_object)
  list(APPEND sources ${rust_object})
endif()

if(CONFIG_HEAP_REALLOC_RUST)
  include(rust)
  pbl_rust_object(pebble_heap_realloc ${CMAKE_CURRENT_SOURCE_DIR}/heap-realloc-rust rust_object)
  list(APPEND sources ${rust_object})
endif()

pbl_library_named(libutil)
pbl_library_kind(stlib)
pbl_library_sources(${sources})
pbl_library_include_directories(.)

if(CONFIG_CRC32_RUST)
  add_dependencies(libutil pebble_crc32_rust_build)
endif()

if(CONFIG_HEAP_REALLOC_RUST)
  add_dependencies(libutil pebble_heap_realloc_rust_build)
endif()
