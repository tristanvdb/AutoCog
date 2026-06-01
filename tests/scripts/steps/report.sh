#!/usr/bin/env bash
#
# Shared report step (coverage builds only). Combines the C++ and Python
# coverage JSON into tests/coverage.md and tests/coverage.json, and checks
# coverage against thresholds.
#
# Environment:
#   BUILD_DIR   (required)  holds coverage_cxx.json / coverage_py.json + logs
#
# python is resolved from PATH; it must be the interpreter autocog was
# installed into (used to locate the install prefix for path normalization).
#
# Exit code propagates from coverage_report.py: nonzero if any gating category
# is below threshold. Under `set -e` this fails the step (and thus the local
# tests-coverage.sh run and the CI test-coverage job). The report writes both
# artifacts before exiting, so a failing run still produces coverage.{md,json}.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

: "${BUILD_DIR:?BUILD_DIR must be set}"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"  # absolute: used after cd "$REPO_ROOT"

# pytest-cov records paths relative to where pytest ran (REPO_ROOT); run the
# generator from the same base so relative paths resolve consistently.
cd "$REPO_ROOT"

# Locate autocog's install prefix so coverage_report.py can strip it.
INSTALL_PREFIX=$(python -c "import autocog, pathlib; print(pathlib.Path(autocog.__file__).parent.parent)")

# Test counts from the step logs (best-effort; absent -> omitted).
CTEST_ARG=(); PYTEST_ARG=()
if [ -f "$BUILD_DIR/ctest.log" ]; then
    # ctest summary line: "<pct>% tests passed, <F> tests failed out of <N>".
    # Passed = N - F. Avoid the naive "first number after 'tests passed'", which
    # would capture the leading percentage (e.g. 100).
    line=$(grep -E "tests failed out of" "$BUILD_DIR/ctest.log" | tail -1 || true)
    if [ -n "${line:-}" ]; then
        failed=$(echo "$line" | sed -E 's/.* ([0-9]+) tests failed out of ([0-9]+).*/\1/')
        outof=$(echo "$line" | sed -E 's/.* ([0-9]+) tests failed out of ([0-9]+).*/\2/')
        [ -n "$outof" ] && CTEST_ARG=(--ctest="$((outof - failed))")
    fi
fi
if [ -f "$BUILD_DIR/pytest.log" ]; then
    n=$(grep -oE "[0-9]+ passed" "$BUILD_DIR/pytest.log" | tail -1 | grep -oE "[0-9]+" || true)
    [ -n "${n:-}" ] && PYTEST_ARG=(--pytest="$n")
fi

python "$SCRIPT_DIR/../coverage_report.py" \
    "$BUILD_DIR/coverage_cxx.json" \
    "$BUILD_DIR/coverage_py.json" \
    --install-prefix="$INSTALL_PREFIX" \
    --outdir="$REPO_ROOT/tests" \
    "${CTEST_ARG[@]}" "${PYTEST_ARG[@]}"
