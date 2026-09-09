#!/usr/bin/env bash
#
# Computational performance benchmark, portable across machines.
#
# Builds a Release (no coverage) tree of the C++ tools — NEVER benchmark a
# Debug/coverage build: the vendored ggml drops to -O0 and every number
# inflates ~100x — then sweeps search parameters over benchmark.stl with
# xfta --perf, writing results-<host>-<model>.{ndjson,md} into this directory.
#
# Usage:
#   benchmarks/compute/run.sh [BUILD_DIR] [MODEL.gguf|--rng] [--quick]
#
#   BUILD_DIR  Release build tree, created/reused (default: build-release/
#              next to this script; kept warm via ccache).
#   MODEL      GGUF model path, or --rng for the harness-overhead floor.
#
# Examples:
#   benchmarks/compute/run.sh                          # RNG floor, default dir
#   benchmarks/compute/run.sh build-rel models/foo.gguf
#   benchmarks/compute/run.sh "" models/foo.gguf --quick

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

BUILD_DIR="${1:-$SCRIPT_DIR/build-release}"
[ -z "$BUILD_DIR" ] && BUILD_DIR="$SCRIPT_DIR/build-release"
MODEL="${2:---rng}"
shift $(( $# > 2 ? 2 : $# )) || true

echo "=== Building Release tools into $BUILD_DIR ==="
cmake -B "$BUILD_DIR" -S "$REPO_ROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DAUTOCOG_BUILD_TESTS=OFF \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache > "$BUILD_DIR.cmake.log" 2>&1
cmake --build "$BUILD_DIR" --target autocog_stlc autocog_ista autocog_xfta \
    -j"$(nproc)" > "$BUILD_DIR.build.log" 2>&1

MODEL_ARGS=(--model "$MODEL")
[ "$MODEL" = "--rng" ] && MODEL_ARGS=(--rng)

exec python3 "$SCRIPT_DIR/sweep.py" --build "$BUILD_DIR" "${MODEL_ARGS[@]}" "$@"
