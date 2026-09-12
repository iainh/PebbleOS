#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
build_dir="${TMPDIR:-/tmp}/pebble-bitblt-palette-benchmark"
crate="$repo_root/src/fw/applib/graphics/1_bit/bitblt-palette-rust"
rm -rf "$build_dir"
mkdir -p "$build_dir"

CARGO_TARGET_DIR="$build_dir/cargo" \
RUSTFLAGS="-C target-cpu=native -C codegen-units=1" \
  cargo rustc --manifest-path "$crate/Cargo.toml" --release --frozen \
  -- --emit=obj="$build_dir/bitblt_palette_rust.o"

"${CC:-clang}" -std=c11 -O3 -DNDEBUG -march=native \
  -I"$repo_root" -I"$repo_root/src/fw" -I"$repo_root/include" \
  "$repo_root/tools/benchmarks/bitblt_palette.c" \
  "$build_dir/bitblt_palette_rust.o" -o "$build_dir/bitblt_palette"

printf 'compiler,%s\n' "$("${CC:-clang}" --version | head -1)"
printf 'rustc,%s\n' "$(rustc --version)"
printf 'machine,%s\n' "$(uname -m)"
"$build_dir/bitblt_palette"
