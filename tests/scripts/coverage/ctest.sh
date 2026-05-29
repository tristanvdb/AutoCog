#!/usr/bin/env bash
set -euo pipefail

: "${BUILD_DIR:?BUILD_DIR must be set}"
: "${VENV_DIR:?VENV_DIR must be set}"

# Golden-comparison ctests shell out to `python3` (golden_compare.py needs
# deepdiff/jsonschema). Put the venv's bin first so python3 resolves to the
# venv interpreter that has the test deps installed.
export PATH="$VENV_DIR/bin:$PATH"

echo "=== Running ctest ==="
cd "$BUILD_DIR"

# Fresh .gcda state for this run
find . -name "*.gcda" -delete

# set -o pipefail (from set -euo pipefail) ensures the tee does not swallow
# a ctest failure — without it the pipe's exit status would be tee's (0).
ctest -j"$(nproc)" --timeout 120 --output-on-failure 2>&1 | tee ctest.log

PASSED=$(grep -E "tests passed" ctest.log | tail -1 | grep -oE "[0-9]+" | head -1 || echo "0")
echo "ctest: $PASSED passed"
