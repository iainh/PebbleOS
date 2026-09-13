# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

pbl_library()
pbl_library_sources(service.c)

if(CONFIG_SERVICE_ACCEL_MANAGER_RUST_SUBSAMPLING)
  include(rust)
  pbl_rust_object(pebble_accel_subsampling
    ${CMAKE_CURRENT_SOURCE_DIR}/subsampling-rust rust_object)
  pbl_library_named(accel_subsampling_rust)
  pbl_library_kind(stlib)
  pbl_library_sources(${rust_object})
  set_target_properties(accel_subsampling_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(accel_subsampling_rust pebble_accel_subsampling_rust_build)
endif()
