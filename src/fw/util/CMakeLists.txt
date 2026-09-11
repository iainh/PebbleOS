# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

file(GLOB_RECURSE sources CONFIGURE_DEPENDS *.c)

if(CONFIG_BASE64_RUST)
  list(REMOVE_ITEM sources ${CMAKE_CURRENT_SOURCE_DIR}/base64.c)
  include(rust)
  pbl_rust_object(pebble_base64 ${CMAKE_CURRENT_SOURCE_DIR}/base64-rust rust_object)
  pbl_library_named(base64_rust)
  pbl_library_kind(stlib)
  pbl_library_sources(${rust_object})
  set_target_properties(base64_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(base64_rust pebble_base64_rust_build)
endif()

pbl_library()
pbl_library_sources(${sources})
