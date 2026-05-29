#!/usr/bin/env bash
set -euo pipefail

: "${BUILD_DIR:?BUILD_DIR must be set}"
: "${VENV_DIR:?VENV_DIR must be set}"

# Golden-comparison ctests shell out to `python3`; ensure it resolves to the
# venv interpreter that has the test deps (deepdiff/jsonschema) installed.
export PATH="$VENV_DIR/bin:$PATH"

echo "=== Running ctest (release) ==="
cd "$BUILD_DIR"

ctest -j"$(nproc)" --timeout 120 --output-on-failure 2>&1 | tee ctest.log

PASSED=$(grep -E "tests passed" ctest.log | tail -1 | grep -oE "[0-9]+" | head -1 || echo "0")
echo "ctest: $PASSED passed"
