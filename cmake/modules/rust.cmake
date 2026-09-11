# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)

function(pbl_rust_object name crate_dir output_var)
  find_program(CARGO cargo)
  find_program(RUSTC rustc)
  if(NOT CARGO OR NOT RUSTC)
    message(FATAL_ERROR
      "Rust modules require Cargo and rustc 1.89.0. "
      "Install the pinned toolchain described in docs/development/getting_started.md.")
  endif()

  execute_process(
    COMMAND ${CARGO} --version
    OUTPUT_VARIABLE cargo_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE cargo_result
  )
  if(NOT cargo_result EQUAL 0 OR NOT cargo_version MATCHES "^cargo 1\\.89\\.0 ")
    message(FATAL_ERROR
      "Rust modules require Cargo 1.89.0; found '${cargo_version}'. "
      "Install the pinned toolchain described in docs/development/getting_started.md.")
  endif()

  execute_process(
    COMMAND ${RUSTC} --version
    OUTPUT_VARIABLE rustc_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE rustc_result
  )
  if(NOT rustc_result EQUAL 0 OR NOT rustc_version MATCHES "^rustc 1\\.89\\.0 ")
    message(FATAL_ERROR
      "Rust modules require rustc 1.89.0; found '${rustc_version}'. "
      "The repository rust-toolchain.toml selects the required version when using rustup.")
  endif()

  get_filename_component(crate_dir ${crate_dir} ABSOLUTE)
  set(rust_target_dir ${CMAKE_CURRENT_BINARY_DIR}/${name}-target)
  set(cargo_target_args "")
  set(rust_object_dir ${rust_target_dir})
  set(rust_flags "--remap-path-prefix=${PBL_BASE}=.")
  set(rust_build_target host)

  if(CMAKE_CROSSCOMPILING)
    if(CONFIG_SOC_NRF52 OR (CONFIG_QEMU AND CONFIG_CORTEX_M4))
      set(rust_target thumbv7em-none-eabi)
      string(APPEND rust_flags " -C target-cpu=cortex-m4")
    elseif(CONFIG_SOC_SF32LB52 OR (CONFIG_QEMU AND CONFIG_CORTEX_M33))
      set(rust_target thumbv8m.main-none-eabi)
      string(APPEND rust_flags " -C target-cpu=cortex-m33")
    else()
      message(FATAL_ERROR "No Rust target for the configured CPU")
    endif()

    set(rust_build_target ${rust_target})
    execute_process(
      COMMAND ${RUSTC} --print target-libdir --target ${rust_target}
      OUTPUT_VARIABLE rust_target_libdir
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE rust_target_result
    )
    file(GLOB rust_core "${rust_target_libdir}/libcore-*.rlib")
    if(NOT rust_target_result EQUAL 0 OR NOT rust_core)
      message(FATAL_ERROR
        "Rust modules require the target '${rust_target}' for toolchain 1.89.0. "
        "Install the targets listed in rust-toolchain.toml.")
    endif()

    set(cargo_target_args --target ${rust_target})
    set(rust_object_dir ${rust_object_dir}/${rust_target})
  endif()

  set(rust_object ${rust_object_dir}/release/${name}.o)
  file(GLOB_RECURSE rust_sources CONFIGURE_DEPENDS ${crate_dir}/src/*.rs)
  add_custom_command(
    OUTPUT ${rust_object}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${rust_object_dir}/release
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${rust_target_dir}
            "RUSTFLAGS=${rust_flags}"
            ${CARGO} rustc --manifest-path ${crate_dir}/Cargo.toml
            --release --frozen ${cargo_target_args}
            -- --emit=obj=${rust_object}
    DEPENDS
      ${crate_dir}/Cargo.toml
      ${crate_dir}/Cargo.lock
      ${rust_sources}
      ${PBL_BASE}/rust-toolchain.toml
    WORKING_DIRECTORY ${PBL_BASE}
    COMMENT "Building Rust module ${name} (${rust_build_target})"
    VERBATIM
  )
  add_custom_target(${name}_rust_build DEPENDS ${rust_object})
  set_source_files_properties(${rust_object} PROPERTIES
    EXTERNAL_OBJECT TRUE
    GENERATED TRUE
  )
  set(${output_var} ${rust_object} PARENT_SCOPE)
endfunction()
