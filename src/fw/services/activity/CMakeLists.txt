# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(sources
  activity.c
  activity_calculators.c
  activity_insights.c
  activity_metrics.c
  activity_sessions.c
  health_util.c
  hr_util.c
  insights_settings.c
  kraepelin/activity_algorithm_kraepelin.c
  kraepelin/kraepelin_algorithm.c
  workout_service.c
)

if(CONFIG_KRAEPELIN_TRANSFORM_RUST)
  include(rust)
  pbl_rust_object(pebble_kraepelin_transform
    ${CMAKE_CURRENT_SOURCE_DIR}/kraepelin/kraepelin-transform-rust
    rust_object)
  pbl_library_named(kraepelin_transform_rust)
  pbl_library_kind(stlib)
  pbl_library_sources(${rust_object})
  set_target_properties(kraepelin_transform_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(kraepelin_transform_rust pebble_kraepelin_transform_rust_build)
else()
  list(APPEND sources kraepelin/kraepelin_transform.c)
endif()

if(CONFIG_KRAEPELIN_PIM_RUST)
  include(rust)
  pbl_rust_object(pebble_kraepelin_pim
    ${CMAKE_CURRENT_SOURCE_DIR}/kraepelin/kraepelin-pim-rust
    rust_object)
  pbl_library_named(kraepelin_pim_rust)
  pbl_library_kind(stlib)
  pbl_library_sources(${rust_object})
  set_target_properties(kraepelin_pim_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(kraepelin_pim_rust pebble_kraepelin_pim_rust_build)
else()
  list(APPEND sources kraepelin/kraepelin_pim.c)
endif()

pbl_library()
pbl_library_sources(${sources})
pbl_service_gettext(activity ${sources})
