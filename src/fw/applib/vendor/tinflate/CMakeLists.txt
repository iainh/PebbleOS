# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_TINFLATE_RUST)
  include(rust)
  pbl_rust_object(pebble_tinflate ${CMAKE_CURRENT_SOURCE_DIR}/tinflate-rust rust_object)
  pbl_library_named(tinflate_rust)
  pbl_library_kind(stlib)
  pbl_library_sources(${rust_object})
  set_target_properties(tinflate_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(tinflate_rust pebble_tinflate_rust_build)
else()
  pbl_library()
  pbl_library_sources(tinflate.c)
endif()
