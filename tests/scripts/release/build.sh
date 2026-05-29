#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

: "${BUILD_DIR:?BUILD_DIR must be set}"
: "${VENV_DIR:?VENV_DIR must be set}"

PREFIX_PATH=""
for d in "$REPO_ROOT/../opt" "$HOME/opt"; do
    [ -d "$d" ] && PREFIX_PATH="$d" && break
done

# Release-mode install: no COVERAGE, default (Release) build type.
PIP_ARGS=(
    "$REPO_ROOT"
    --no-build-isolation
    --no-deps
    --config-settings="build-dir=$BUILD_DIR"
    --config-settings="cmake.define.CMAKE_BUILD_TYPE=Release"
    --config-settings="cmake.define.AUTOCOG_TUNED=OFF"
    --config-settings="cmake.define.CMAKE_CXX_COMPILER_LAUNCHER=ccache"
    --config-settings="cmake.define.CMAKE_C_COMPILER_LAUNCHER=ccache"
)
[ -n "$PREFIX_PATH" ] && PIP_ARGS+=(--config-settings="cmake.define.CMAKE_PREFIX_PATH=$PREFIX_PATH")

echo "=== Building (release) ==="
"$VENV_DIR/bin/pip" install "${PIP_ARGS[@]}"
