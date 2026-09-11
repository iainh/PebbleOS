# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(excl)
if(NOT CONFIG_BT_ANCS_CLIENT)
  list(APPEND excl ble/kernel_le_client/ancs/*.c)
endif()
if(NOT CONFIG_BT_AMS_CLIENT)
  list(APPEND excl ble/kernel_le_client/ams/*.c)
endif()
file(GLOB_RECURSE sources CONFIGURE_DEPENDS *.c)
pbl_filter_sources(sources EXCLUDE ${excl})

if(CONFIG_ANCS_UTIL_RUST AND CONFIG_BT_ANCS_CLIENT)
  list(REMOVE_ITEM sources
    ${CMAKE_CURRENT_SOURCE_DIR}/ble/kernel_le_client/ancs/ancs_util.c)
  include(rust)
  pbl_rust_object(pebble_ancs_util
    ${CMAKE_CURRENT_SOURCE_DIR}/ble/kernel_le_client/ancs/ancs-util-rust
    rust_object)
  pbl_library_named(ancs_util_rust)
  pbl_library_kind(stlib)
  pbl_library_sources(${rust_object})
  set_target_properties(ancs_util_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(ancs_util_rust pebble_ancs_util_rust_build)
endif()

pbl_library()
pbl_library_sources(${sources})
