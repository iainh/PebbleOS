# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(sources
  compositor.c
  compositor_display.c
  compositor_scaler_benchmark.c
  compositor_transitions.c
  screenshot_pp.c
  default/compositor_dot_transitions.c
  default/compositor_launcher_app_transitions.c
  default/compositor_peek_transitions.c
  default/compositor_port_hole_transitions.c
  default/compositor_round_flip_transitions.c
  default/compositor_shutter_transitions.c
  default/compositor_slide_transitions.c
  legacy/compositor_app_slide_transitions.c
)
if(CONFIG_SCREEN_COLOR_DEPTH_BITS EQUAL 1)
  list(APPEND sources legacy/compositor_modal_slide_transitions.c)
else()
  list(APPEND sources default/compositor_modal_transitions.c)
endif()

if(CONFIG_COMPOSITOR_DAMAGE_RUST)
  include(rust)
  pbl_rust_object(pebble_compositor_damage
    ${CMAKE_CURRENT_SOURCE_DIR}/damage-tracker-rust rust_object)
  pbl_library_named(compositor_damage_rust)
  pbl_library_kind(stlib)
  pbl_library_sources(${rust_object})
  set_target_properties(compositor_damage_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(compositor_damage_rust pebble_compositor_damage_rust_build)
endif()

if(CONFIG_COMPOSITOR_LEGACY_SCALER_RUST)
  include(rust)
  pbl_rust_object(pebble_compositor_legacy_scaler
    ${CMAKE_CURRENT_SOURCE_DIR}/legacy-scaler-rust rust_object)
  pbl_library_named(compositor_legacy_scaler_rust)
  pbl_library_kind(stlib)
  pbl_library_sources(${rust_object})
  set_target_properties(compositor_legacy_scaler_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(compositor_legacy_scaler_rust pebble_compositor_legacy_scaler_rust_build)
endif()

pbl_library()
pbl_library_sources(${sources})
