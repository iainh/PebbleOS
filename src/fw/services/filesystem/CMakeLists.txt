# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(sources
  app_file.c
  flash_translation.c
  pfs.c
)

if(CONFIG_PFS_LOOKUP_RUST)
  include(rust)
  pbl_rust_object(pebble_pfs_lookup
    ${CMAKE_CURRENT_SOURCE_DIR}/pfs-lookup-rust rust_object)
  pbl_library_named(pfs_lookup_rust)
  pbl_library_kind(stlib)
  pbl_library_sources(${rust_object})
  set_target_properties(pfs_lookup_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(pfs_lookup_rust pebble_pfs_lookup_rust_build)
endif()

pbl_library()
pbl_library_sources(${sources})
