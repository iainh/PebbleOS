#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

repo=$(git rev-parse --show-toplevel)
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT

export CARGO_TARGET_DIR="$build_dir/cargo"
export RUSTFLAGS="--remap-path-prefix=$repo=."
cargo rustc --manifest-path \
  "$repo/src/fw/services/activity/kraepelin/kraepelin-transform-rust/Cargo.toml" \
  --release --frozen -- --emit="obj=$build_dir/rust.o"

common_flags=(-Os -DNDEBUG -I"$repo/include" -I"$repo/src/fw")
clang "${common_flags[@]}" \
  -Dkraepelin_transform_window=kraepelin_transform_c_window \
  -Dkraepelin_transform_fft=kraepelin_transform_c_fft \
  -Dkraepelin_transform_magnitudes=kraepelin_transform_c_magnitudes \
  -Dkraepelin_transform_epoch=kraepelin_transform_c_epoch \
  -c "$repo/src/fw/services/activity/kraepelin/kraepelin_transform.c" \
  -o "$build_dir/c.o"
clang "${common_flags[@]}" \
  "$repo/tools/activity/kraepelin_transform_benchmark.c" \
  "$repo/lib/util/trig.c" "$build_dir/c.o" "$build_dir/rust.o" \
  -o "$build_dir/benchmark"

echo "Host: $(uname -m), clang $(clang --version | head -1), rustc $(rustc --version)"
echo "Both implementations: size optimized, one process, CLOCK_MONOTONIC_RAW, 100000 iterations"
if command -v taskset >/dev/null && taskset -c 0 true 2>/dev/null; then
  taskset -c 0 "$build_dir/benchmark"
else
  "$build_dir/benchmark"
fi
echo
echo "Object sizes (text data bss decimal):"
size "$build_dir/c.o" "$build_dir/rust.o"
