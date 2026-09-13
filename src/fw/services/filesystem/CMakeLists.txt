# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(sources app_file.c flash_translation.c)

if(CONFIG_PFS_RUST)
  include(rust)
  pbl_rust_object(pebble_pfs ${CMAKE_CURRENT_SOURCE_DIR}/pfs-rust rust_object rust_archive)
  add_library(pfs_rust STATIC IMPORTED GLOBAL)
  set_target_properties(pfs_rust PROPERTIES
    IMPORTED_LOCATION ${rust_archive}
    INTERFACE_LINK_DEPENDS ${rust_object}
  )
  add_dependencies(pfs_rust pebble_pfs_rust_build)
  set_property(GLOBAL APPEND PROPERTY PBL_STATIC_LIBS pfs_rust)
  list(APPEND sources pfs-rust/pfs_shim.c)
else()
  list(APPEND sources pfs.c)
endif()

pbl_library()
pbl_library_sources(${sources})
