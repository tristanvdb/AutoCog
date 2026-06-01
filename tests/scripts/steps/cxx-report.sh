#!/usr/bin/env bash
#
# Shared C++ coverage step (coverage builds only). Runs gcovr over the .gcda
# accumulated by ctest and pytest, emitting BUILD_DIR/coverage_cxx.json.
#
# Environment:
#   BUILD_DIR   (required)  cmake build directory (holds .gcda + output json)
#
# gcovr is resolved from PATH (it comes from the test group).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

: "${BUILD_DIR:?BUILD_DIR must be set}"
# Normalize to absolute: the script cd's into BUILD_DIR below, so relative
# BUILD_DIR (e.g. "build" in CI) must be resolved first or output paths break.
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"

echo "=== Generating C++ coverage ==="
cd "$BUILD_DIR"

gcovr -r "$REPO_ROOT" . \
    --filter "$REPO_ROOT/libs/" \
    --filter "$REPO_ROOT/tools/" \
    --filter "$REPO_ROOT/bindings/" \
    --gcov-ignore-errors=no_working_dir_found \
    --json "$BUILD_DIR/coverage_cxx.json" \
    2> "$BUILD_DIR/gcovr.log"

if [ -s "$BUILD_DIR/gcovr.log" ]; then
    echo "gcovr produced warnings/errors (see $BUILD_DIR/gcovr.log):"
    head -20 "$BUILD_DIR/gcovr.log"
fi
