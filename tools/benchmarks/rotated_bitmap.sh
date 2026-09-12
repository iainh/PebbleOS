#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
build_dir="${1:-$repo_root/build-benchmark-rotated-bitmap}"
platforms=(asterix obelix gabbro)
targets=()

for platform in "${platforms[@]}"; do
  targets+=("test_graphics_rotated_bitmap_differential_${platform}")
done

cmake -S "$repo_root/tests" -B "$build_dir" -GNinja \
  -DPBL_TEST_IMAGES=OFF -DCONFIG_ROTATED_BITMAP_RUST=ON
cmake --build "$build_dir" --target "${targets[@]}"

for platform in "${platforms[@]}"; do
  binary="$build_dir/fw/graphics/test_graphics_rotated_bitmap_differential_${platform}/runme_${platform}"
  result="$($binary 2>/dev/null | sed -n 's/^ROTATED_BITMAP_BENCHMARK //p')"
  read -r c_us rust_us < <(sed -n 's/.*c_us=\([0-9]*\) rust_us=\([0-9]*\).*/\1 \2/p' <<<"$result")
  metrics="$(python3 - "$c_us" "$rust_us" <<'PY'
import sys

c_us, rust_us = map(int, sys.argv[1:])
print(f"speedup={c_us / rust_us:.2f}x reduction={(c_us - rust_us) / c_us * 100:.2f}%")
PY
)"
  printf '%s %s\n' "$result" "$metrics"
done
