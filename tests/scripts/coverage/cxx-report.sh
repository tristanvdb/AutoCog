#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

: "${BUILD_DIR:?BUILD_DIR must be set}"
: "${VENV_DIR:?VENV_DIR must be set}"

echo "=== Generating C++ coverage ==="
cd "$BUILD_DIR"

# gcovr reads .gcda accumulated by both ctest and pytest runs.
# Capture stderr to a log so warnings are preserved but don't pollute output.
"$VENV_DIR/bin/gcovr" -r "$REPO_ROOT" . \
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
