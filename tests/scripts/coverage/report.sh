#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

: "${BUILD_DIR:?BUILD_DIR must be set}"
: "${VENV_DIR:?VENV_DIR must be set}"

REPORT="$REPO_ROOT/tests/coverage.md"

# pytest-cov records file paths relative to the directory pytest ran in
# ($REPO_ROOT, see pytest.sh). Run the report generator from the same base so
# coverage_report.py resolves those relative paths consistently with the
# install prefix below.
cd "$REPO_ROOT"

# Ask Python where autocog actually lives, so coverage_report.py can strip that
# prefix for path normalization, instead of guessing from a "modules" segment.
INSTALL_PREFIX=$("$VENV_DIR/bin/python" -c "
import autocog
import pathlib
print(pathlib.Path(autocog.__file__).parent.parent)
")

"$VENV_DIR/bin/python" "$SCRIPT_DIR/../coverage_report.py" \
    "$BUILD_DIR/coverage_cxx.json" \
    "$BUILD_DIR/coverage_py.json" \
    --install-prefix="$INSTALL_PREFIX" \
    -o "$REPORT"

echo "Coverage report: $REPORT"
