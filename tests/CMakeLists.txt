# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
#
# PebbleOS unit tests.
#
#   cmake -B build-test -S tests -GNinja
#   cmake --build build-test
#   ctest --test-dir build-test --output-on-failure
#
# Optimized glyph benchmark (repeat with CONFIG_GLYPH_RASTER_RUST=ON):
#   cmake -B build-bench -S tests -GNinja -DGLYPH_RASTER_BENCHMARK=ON
#   cmake --build build-bench --target test_text_render
#   build-bench/fw/applib/test_text_render/runme
#
# These build for the host, not the watch, so they are their own project
# with their own build directory; the firmware build is the one rooted at
# the repository's top-level CMakeLists.txt.

cmake_minimum_required(VERSION 3.21)

get_filename_component(PBL_BASE ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
list(APPEND CMAKE_MODULE_PATH ${PBL_BASE}/cmake/modules)

# The tests are built with clang everywhere; some of them lean on its
# diagnostics and on what it accepts.
if(NOT CMAKE_C_COMPILER AND NOT DEFINED ENV{CC})
  set(CMAKE_C_COMPILER clang)
endif()

find_program(CCACHE ccache)
if(CCACHE)
  set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE})
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(pebbleos-tests LANGUAGES C)

option(GLYPH_RASTER_BENCHMARK "Build the optimized host glyph benchmark" OFF)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PYTHON_EXECUTABLE ${Python3_EXECUTABLE})

include(extensions)
include(pbl_test)

if(GLYPH_RASTER_BENCHMARK)
  list(REMOVE_ITEM PBL_TEST_C_FLAGS -O0)
  list(APPEND PBL_TEST_C_FLAGS -O3)
endif()

option(PBL_TEST_IMAGES "Build the image fixtures the graphics tests render against" ON)
option(PBL_TEST_COVERAGE "Instrument the tests for gcov/lcov coverage" OFF)
set(PBL_TEST_IMAGE_BATCHES 16 CACHE STRING
    "How many jobs the image fixtures are converted in")

enable_testing()
pbl_init()
pbl_test_init()

add_compile_options(${PBL_TEST_C_FLAGS})
set(PBL_TEST_DEFINES
  "CLAR_FIXTURE_PATH=\"${PBL_BASE}/tests/fixtures/\""
  CONFIG_LOG=1
  CONFIG_KERNEL_NUM_PRIORITIES=5
  CONFIG_KERNEL_THREAD_TLS_SLOTS=2
  CONFIG_KERNEL_STACK_ALIGN=8
  CONFIG_KERNEL_MUTEX_LOCK_LR=1
  CONFIG_KERNEL_TICK_HZ=1024
  CONFIG_KERNEL_IRQ_PRIO_KERNEL=255
  CONFIG_KERNEL_IRQ_PRIO_MAX_SYSCALL=191
)
if(CONFIG_BITBLT_PALETTE_RUST)
  list(APPEND PBL_TEST_DEFINES CONFIG_BITBLT_PALETTE_RUST=1)
endif()
add_compile_definitions(${PBL_TEST_DEFINES})

if(PBL_TEST_COVERAGE)
  add_compile_options(-fprofile-arcs -ftest-coverage)
  add_link_options(--coverage)
  list(APPEND PBL_TEST_C_FLAGS -fprofile-arcs -ftest-coverage)
endif()

# Apple's ARM64 linker uses chained fixups, which need pointer-aligned
# relocations; packed structs with pointer members cannot supply them.
if(APPLE)
  add_link_options(-Wl,-no_fixup_chains)
endif()

# The generated headers every library may include. The firmware build
# fills this with far more; here it is only the protobuf schemas.
add_custom_target(pbl_generated_headers)

# FIXME: remove include/pbl once all modules use the prefix
pbl_include_directories(${PBL_BASE}/include ${PBL_BASE}/include/pbl)

add_subdirectory(${PBL_BASE}/third_party/nanopb ${CMAKE_BINARY_DIR}/third_party/nanopb)
add_subdirectory(${PBL_BASE}/src/idl ${CMAKE_BINARY_DIR}/src/idl)
add_subdirectory(${PBL_BASE}/lib/util ${CMAKE_BINARY_DIR}/lib/util)
add_subdirectory(${PBL_BASE}/lib/btutil ${CMAKE_BINARY_DIR}/lib/btutil)
add_subdirectory(${PBL_BASE}/tools/pdc2png ${CMAKE_BINARY_DIR}/tools/pdc2png)
set(PBL_IDL_INCLUDE_DIR ${CMAKE_BINARY_DIR}/src/idl)

add_subdirectory(vendor/duma)

# The image fixtures. There are a couple of thousand of them, converted
# in batches so that ninja schedules them alongside the compiles instead
# of a single job running a pool of its own against the build's.
set(pbl_test_images_stamp ${CMAKE_BINARY_DIR}/test_images.stamp)
if(PBL_TEST_IMAGES)
  set(images ${CMAKE_CURRENT_SOURCE_DIR}/test_images)
  file(GLOB fixtures CONFIGURE_DEPENDS ${images}/*)
  list(LENGTH fixtures fixture_count)
  math(EXPR batch_size "(${fixture_count} + ${PBL_TEST_IMAGE_BATCHES} - 1)
                        / ${PBL_TEST_IMAGE_BATCHES}")

  set(stamps "")
  foreach(batch RANGE 1 ${PBL_TEST_IMAGE_BATCHES})
    math(EXPR first "(${batch} - 1) * ${batch_size}")
    if(first GREATER_EQUAL fixture_count)
      break()
    endif()
    list(SUBLIST fixtures ${first} ${batch_size} entries)
    # A fixture may be a directory of SVGs making up one animation; the
    # batch has to rebuild when anything inside it changes.
    set(inputs ${entries})
    foreach(entry ${entries})
      if(IS_DIRECTORY ${entry})
        file(GLOB_RECURSE contents CONFIGURE_DEPENDS ${entry}/*)
        list(APPEND inputs ${contents})
      endif()
    endforeach()

    set(listing ${CMAKE_BINARY_DIR}/test_images.${batch}.txt)
    set(stamp ${CMAKE_BINARY_DIR}/test_images.${batch}.stamp)
    string(REPLACE ";" "\n" contents "${entries}")
    file(CONFIGURE OUTPUT ${listing} CONTENT "${contents}\n")
    add_custom_command(
      OUTPUT ${stamp}
      COMMAND ${PYTHON_EXECUTABLE} ${PBL_BASE}/tools/cmake/test_images.py convert
              --src ${images} --out ${PBL_TEST_IMAGE_DIR}
              --files ${listing} --stamp ${stamp}
      DEPENDS ${inputs} ${PBL_BASE}/tools/cmake/test_images.py
      COMMENT "Converting test image fixtures (${batch}/${PBL_TEST_IMAGE_BATCHES})"
      VERBATIM
    )
    list(APPEND stamps ${stamp})
  endforeach()

  # The reference PDCs are rendered by the pdc2png host tool. Only this
  # step depends on it, so touching the graphics code does not send every
  # other fixture through the converter again.
  add_custom_command(
    OUTPUT ${pbl_test_images_stamp}
    COMMAND ${PYTHON_EXECUTABLE} ${PBL_BASE}/tools/cmake/test_images.py pdc
            --src ${images} --out ${PBL_TEST_IMAGE_DIR}
            --pdc2png $<TARGET_FILE:pdc2png> --stamp ${pbl_test_images_stamp}
    DEPENDS ${stamps} pdc2png ${PBL_BASE}/tools/cmake/test_images.py
    COMMENT "Rendering the reference PDC fixtures"
    VERBATIM
  )
  add_custom_target(pbl_test_images DEPENDS ${pbl_test_images_stamp})
else()
  add_custom_target(pbl_test_images)
endif()

# Tests that fail a render write what they produced here for comparison,
# so the directory starts every run empty.
add_test(NAME pbl_test_failed_dir COMMAND ${CMAKE_COMMAND}
  -DDIR=${PBL_TEST_FAILED_DIR} -P ${PBL_BASE}/tools/cmake/reset_dir.cmake)
set_tests_properties(pbl_test_failed_dir PROPERTIES FIXTURES_SETUP pbl_test_failed_dir)

# Tests that do not build. Kept here rather than deleted, so what is
# missing stays visible.
pbl_test_broken(
  test_graphics_draw_circle_8bit
  test_graphics_fill_circle_8bit
  test_graphics_draw_line
  test_graphics_draw_rotated_bitmap
  # Crash (round-display segfault) and build (rect/round, platform)
  # issues are fixed; what is left is missing gabbro golden .pbi images
  # that need visual review before they can be regenerated.
  test_gap_le_connect
)

if(CONFIG_BASE64_RUST)
  include(rust)
  pbl_rust_object(pebble_base64 ${PBL_BASE}/src/fw/util/base64-rust rust_object)
  add_library(test_base64_rust STATIC ${rust_object})
  set_target_properties(test_base64_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(test_base64_rust pebble_base64_rust_build)
  set(base64_libs test_base64_rust)
else()
  set(base64_sources src/fw/util/base64.c)
endif()

if(CONFIG_COBS_RUST)
  include(rust)
  pbl_rust_object(pebble_cobs ${PBL_BASE}/src/fw/console/cobs-rust rust_object)
  add_library(test_cobs_rust STATIC ${rust_object})
  set_target_properties(test_cobs_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(test_cobs_rust pebble_cobs_rust_build)
  set(cobs_libs test_cobs_rust)
else()
  set(cobs_sources src/fw/console/cobs.c)
endif()

if(CONFIG_ANCS_UTIL_RUST)
  include(rust)
  pbl_rust_object(pebble_ancs_util
    ${PBL_BASE}/src/fw/comm/ble/kernel_le_client/ancs/ancs-util-rust
    rust_object)
  add_library(test_ancs_util_rust STATIC ${rust_object})
  set_target_properties(test_ancs_util_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(test_ancs_util_rust pebble_ancs_util_rust_build)
  set(ancs_util_libs test_ancs_util_rust)
else()
  set(ancs_util_sources src/fw/comm/ble/kernel_le_client/ancs/ancs_util.c)
endif()

if(CONFIG_KRAEPELIN_PIM_RUST)
  include(rust)
  pbl_rust_object(pebble_kraepelin_pim
    ${PBL_BASE}/src/fw/services/activity/kraepelin/kraepelin-pim-rust
    rust_object)
  add_library(test_kraepelin_pim_rust STATIC ${rust_object})
  set_target_properties(test_kraepelin_pim_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(test_kraepelin_pim_rust pebble_kraepelin_pim_rust_build)
  set(kraepelin_pim_libs test_kraepelin_pim_rust)
else()
  set(kraepelin_pim_sources src/fw/services/activity/kraepelin/kraepelin_pim.c)
endif()

if(CONFIG_KRAEPELIN_TRANSFORM_RUST)
  include(rust)
  pbl_rust_object(pebble_kraepelin_transform
    ${PBL_BASE}/src/fw/services/activity/kraepelin/kraepelin-transform-rust
    rust_object)
  add_library(test_kraepelin_transform_rust STATIC ${rust_object})
  set_target_properties(test_kraepelin_transform_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(test_kraepelin_transform_rust pebble_kraepelin_transform_rust_build)
  set(kraepelin_transform_libs test_kraepelin_transform_rust)
else()
  set(kraepelin_transform_sources src/fw/services/activity/kraepelin/kraepelin_transform.c)
endif()

if(CONFIG_NOTIFICATION_COMPACTION_RUST)
  include(rust)
  pbl_rust_object(pebble_notification_compaction
    ${PBL_BASE}/src/fw/services/notifications/notification-compaction-rust
    rust_object)
  add_library(test_notification_compaction_rust STATIC ${rust_object})
  set_target_properties(test_notification_compaction_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(test_notification_compaction_rust pebble_notification_compaction_rust_build)
  set(notification_compaction_defines CONFIG_NOTIFICATION_COMPACTION_RUST)
  set(notification_compaction_libs test_notification_compaction_rust)
endif()

if(CONFIG_TINFLATE_RUST)
  include(rust)
  pbl_rust_object(pebble_tinflate
    ${PBL_BASE}/src/fw/applib/vendor/tinflate/tinflate-rust
    rust_object)
  add_library(test_tinflate_rust STATIC ${rust_object})
  set_target_properties(test_tinflate_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(test_tinflate_rust pebble_tinflate_rust_build)
  set(tinflate_libs test_tinflate_rust)
  set(tinflate_defines CONFIG_TINFLATE_RUST)
else()
  set(tinflate_sources src/fw/applib/vendor/tinflate/tinflate.c)
endif()

if(CONFIG_BITBLT_PALETTE_RUST)
  include(rust)
  pbl_rust_object(pebble_bitblt_palette
    ${PBL_BASE}/src/fw/applib/graphics/1_bit/bitblt-palette-rust
    rust_object)
  add_library(test_bitblt_palette_rust STATIC ${rust_object})
  set_target_properties(test_bitblt_palette_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(test_bitblt_palette_rust pebble_bitblt_palette_rust_build)
endif()

if(CONFIG_GLYPH_RASTER_RUST)
  include(rust)
  pbl_rust_object(pebble_glyph_raster
    ${PBL_BASE}/src/fw/applib/graphics/glyph-raster-rust rust_object)
  add_library(test_glyph_raster_rust STATIC ${rust_object})
  set_target_properties(test_glyph_raster_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(test_glyph_raster_rust pebble_glyph_raster_rust_build)
  set(glyph_raster_libs test_glyph_raster_rust)
  set(glyph_raster_defines CONFIG_GLYPH_RASTER_RUST)
endif()

if(CONFIG_ROTATED_BITMAP_RUST)
  include(rust)
  pbl_rust_object(pebble_rotated_bitmap
    ${PBL_BASE}/src/fw/applib/graphics/rotated-bitmap-rust rust_object)
  add_library(test_rotated_bitmap_rust STATIC ${rust_object})
  set_target_properties(test_rotated_bitmap_rust PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(test_rotated_bitmap_rust pebble_rotated_bitmap_rust_build)
endif()

if(CONFIG_BITBLT_RUST)
  include(rust)
  pbl_rust_object(pebble_bitblt
    ${PBL_BASE}/src/fw/applib/graphics/8_bit/bitblt-rust rust_object)
  add_library(test_bitblt_rust_impl STATIC ${rust_object})
  set_target_properties(test_bitblt_rust_impl PROPERTIES LINKER_LANGUAGE C)
  add_dependencies(test_bitblt_rust_impl pebble_bitblt_rust_build)
endif()

# Every directory that declares tests.
file(GLOB_RECURSE pbl_test_dirs CONFIGURE_DEPENDS
     RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} */CMakeLists.txt)
list(SORT pbl_test_dirs)
foreach(list_file ${pbl_test_dirs})
  get_filename_component(dir ${list_file} DIRECTORY)
  if(NOT dir STREQUAL "vendor/duma")
    add_subdirectory(${dir})
  endif()
endforeach()

if(CONFIG_TINFLATE_RUST)
  get_property(test_ids GLOBAL PROPERTY PBL_TESTS)
  foreach(test_id ${test_ids})
    get_property(test_sources GLOBAL PROPERTY PBL_TEST_${test_id}_SOURCES)
    if("src/fw/applib/vendor/tinflate/tinflate.c" IN_LIST test_sources)
      list(REMOVE_ITEM test_sources src/fw/applib/vendor/tinflate/tinflate.c)
      set_property(GLOBAL PROPERTY PBL_TEST_${test_id}_SOURCES "${test_sources}")
      get_property(test_libs GLOBAL PROPERTY PBL_TEST_${test_id}_LIBS)
      list(APPEND test_libs test_tinflate_rust)
      set_property(GLOBAL PROPERTY PBL_TEST_${test_id}_LIBS "${test_libs}")
    endif()
  endforeach()
endif()

if(CONFIG_BITBLT_PALETTE_RUST)
  get_property(test_ids GLOBAL PROPERTY PBL_TESTS)
  foreach(test_id ${test_ids})
    get_property(test_sources GLOBAL PROPERTY PBL_TEST_${test_id}_SOURCES)
    if("src/fw/applib/graphics/@BITDEPTH@_bit/bitblt_private.c" IN_LIST test_sources)
      get_property(test_libs GLOBAL PROPERTY PBL_TEST_${test_id}_LIBS)
      list(APPEND test_libs test_bitblt_palette_rust)
      set_property(GLOBAL PROPERTY PBL_TEST_${test_id}_LIBS "${test_libs}")
    endif()
  endforeach()
endif()

if(CONFIG_BITBLT_RUST)
  get_property(test_ids GLOBAL PROPERTY PBL_TESTS)
  foreach(test_id ${test_ids})
    get_property(test_sources GLOBAL PROPERTY PBL_TEST_${test_id}_SOURCES)
    if("src/fw/applib/graphics/8_bit/bitblt_private.c" IN_LIST test_sources)
      get_property(test_defines GLOBAL PROPERTY PBL_TEST_${test_id}_DEFINES)
      list(APPEND test_defines CONFIG_BITBLT_RUST)
      set_property(GLOBAL PROPERTY PBL_TEST_${test_id}_DEFINES "${test_defines}")
      get_property(test_libs GLOBAL PROPERTY PBL_TEST_${test_id}_LIBS)
      list(APPEND test_libs test_bitblt_rust_impl)
      set_property(GLOBAL PROPERTY PBL_TEST_${test_id}_LIBS "${test_libs}")
    endif()
  endforeach()
endif()

pbl_test_finalize()

if(GLYPH_RASTER_BENCHMARK)
  target_compile_definitions(test_text_render PRIVATE GLYPH_RASTER_BENCHMARK)
endif()
