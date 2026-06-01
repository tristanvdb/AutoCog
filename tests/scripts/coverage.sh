#!/usr/bin/env bash
#
###########################
#
# Coverage analysis for AutoCog: build, ctest, pytest, generate report.
#
# Usage:
#   tests/scripts/coverage.sh [BUILD_DIR] [VENV_DIR]
#
# If BUILD_DIR is supplied: reused across runs (ccache stays hot), cleaned at start.
# If omitted: temporary, removed on exit.
#
# If VENV_DIR is supplied: user-managed (not cleaned, not refreshed).
# If omitted: temporary, removed on exit.
#
# Output: tests/coverage.md
#
###########################
#
# Claude Agent: for stable builds that leverage ccache always use the following commands:
#
#   export BUILD_DIR="/home/claude/AutoCog/build" VENV_DIR="/home/claude/AutoCog/.venv" && cd /home/claude/AutoCog && timeout 290s tests/scripts/coverage.sh $BUILD_DIR $VENV_DIR && echo "Done" || ccache -s
#
# Reusing the same BUILD_DIR/VENV_DIR on every invocation keeps
# the cache warm: a cold full build is slow and may not finish in one pass, but
# the compiled objects persist in ccache, so each repeated identical run resumes
# via cache hits and advances until it prints "Done" (and writes coverage.md).
# On a timeout/failure the `|| ccache -s` shows the cache warming across runs.
#
###########################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# --- BUILD_DIR ------------------------------------------------------------
if [[ -n "${1:-}" ]]; then
    export BUILD_DIR="$(readlink -f "$1")"
    BUILD_PERSISTENT=true
    # Wipe object/.gcda state but keep ccache (cache lives at ~/.cache/ccache).
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
else
    export BUILD_DIR="$(mktemp -d -t autocog-build.XXXXXX)"
    BUILD_PERSISTENT=false
fi

# --- VENV_DIR -------------------------------------------------------------
if [[ -n "${2:-}" ]]; then
    export VENV_DIR="$(readlink -f "$2")"
    VENV_PERSISTENT=true
    if [[ ! -f "$VENV_DIR/bin/python3" ]]; then
        echo "Error: VENV_DIR '$VENV_DIR' is not a venv (no bin/python3). Create with: python3 -m venv $VENV_DIR" >&2
        exit 1
    fi
else
    export VENV_DIR="$(mktemp -d -t autocog-venv.XXXXXX)"
    VENV_PERSISTENT=false
    python3 -m venv "$VENV_DIR"
    "$VENV_DIR/bin/pip" install -q --upgrade pip
    "$VENV_DIR/bin/pip" install -q -r "$REPO_ROOT/tests/requirements.txt" \
                                       scikit-build-core pybind11
fi

cleanup() {
    if ! $BUILD_PERSISTENT; then rm -rf "$BUILD_DIR"; fi
    if ! $VENV_PERSISTENT; then rm -rf "$VENV_DIR"; fi
}
trap cleanup EXIT

echo "BUILD_DIR=$BUILD_DIR (persistent=$BUILD_PERSISTENT)"
echo "VENV_DIR=$VENV_DIR  (persistent=$VENV_PERSISTENT)"
echo ""

# --- Run sub-scripts (each propagates failure via set -e + pipefail) -------
"$SCRIPT_DIR/coverage/build.sh"
"$SCRIPT_DIR/coverage/ctest.sh"
"$SCRIPT_DIR/coverage/pytest.sh"
"$SCRIPT_DIR/coverage/cxx-report.sh"
"$SCRIPT_DIR/coverage/report.sh"

echo ""
echo "Coverage report: $REPO_ROOT/tests/coverage.md"
