#!/usr/bin/env bash
#
# Shared ctest step. Runs the C++ test suite from BUILD_DIR, accumulating
# .gcda when the build was coverage-instrumented.
#
# Environment:
#   BUILD_DIR   (required)  cmake build directory
#
# python3 is resolved from PATH (golden-comparison ctests shell out to
# golden_compare.py, which needs deepdiff/jsonschema). Top scripts put the
# venv's bin on PATH; CI uses system python with the test group installed.

set -euo pipefail

: "${BUILD_DIR:?BUILD_DIR must be set}"

echo "=== Running ctest ==="
cd "$BUILD_DIR"

# Fresh .gcda state for this run (no-op for non-coverage builds).
find . -name "*.gcda" -delete

# set -o pipefail ensures a ctest failure is not swallowed by tee.
ctest -j"$(nproc)" --timeout 120 --output-on-failure 2>&1 | tee ctest.log

PASSED=$(grep -E "tests failed out of" ctest.log | tail -1 \
    | sed -E 's/.* ([0-9]+) tests failed out of ([0-9]+).*/\2 \1/' \
    | awk '{print $1 - $2}')
echo "ctest: ${PASSED:-?} passed"
