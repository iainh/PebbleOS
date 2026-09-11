# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(excl)
if(NOT CONFIG_PROMPT)
  list(APPEND excl
    prompt.c
    prompt_commands.c
  )
endif()
file(GLOB sources CONFIGURE_DEPENDS *.c)
pbl_filter_sources(sources EXCLUDE ${excl})

if(CONFIG_COBS_RUST)
  list(REMOVE_ITEM sources ${CMAKE_CURRENT_SOURCE_DIR}/cobs.c)
  include(rust)
  pbl_rust_object(pebble_cobs ${CMAKE_CURRENT_SOURCE_DIR}/cobs-rust rust_object)
  pbl_library_named(cobs_rust)
  pbl_library_kind(stlib)
  pbl_library_sources(${rust_object})
  set_target_properties(cobs_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(cobs_rust pebble_cobs_rust_build)
endif()

pbl_library()
pbl_library_sources(${sources})
