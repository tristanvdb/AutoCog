#!/usr/bin/env bash
# Machine setup — the first of the two commands that start any experiment
# campaign (then: experiments/calibrate.sh, then the campaign's script).
#
# Assumes a clean clone with submodules initialized:
#   git submodule update --init --recursive
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO"

if [ ! -e vendors/llama/include/llama.h ]; then
    echo "vendored submodules missing — run: git submodule update --init --recursive" >&2
    exit 1
fi

echo "=== system dependencies ==="
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo > /dev/null 2>&1; then
        SUDO="sudo"
    else
        echo "warning: not root and no sudo — skipping package installation" >&2
    fi
fi

need_tools() {  # commands that must exist before we build
    for c in g++ make cmake git curl python3; do
        command -v "$c" > /dev/null 2>&1 || return 0
    done
    python3 -m venv --help > /dev/null 2>&1 || return 0
    return 1
}

if need_tools; then
    if command -v apt-get > /dev/null 2>&1; then
        $SUDO apt-get update -qq
        $SUDO apt-get install -y -qq build-essential cmake git curl \
            python3-venv python3-dev python3-pip ccache
    elif command -v dnf > /dev/null 2>&1; then
        $SUDO dnf install -y -q gcc-c++ make cmake git curl \
            python3-devel python3-pip
        $SUDO dnf install -y -q ccache || true   # EPEL-only on RHEL-likes; optional
    else
        echo "warning: no apt-get/dnf found — install a C++ toolchain, cmake, curl," >&2
        echo "         and python3 (with venv) manually, then re-run" >&2
        exit 1
    fi
else
    echo "toolchain present — nothing to install"
fi

# CUDA toolkit is never auto-installed (driver/toolkit setup is image
# business); we only diagnose the half-configured case.
if command -v nvidia-smi > /dev/null 2>&1 && ! command -v nvcc > /dev/null 2>&1; then
    echo "warning: GPU present but nvcc (CUDA toolkit) missing — the CUDA build" >&2
    echo "         will likely fail; use a CUDA-toolkit image or install it first" >&2
fi

CUDA_ARGS=()
if command -v nvidia-smi > /dev/null 2>&1; then
    echo "=== CUDA GPU detected — building with GGML_CUDA ==="
    nvidia-smi --query-gpu=name,memory.total --format=csv,noheader || true
    CUDA_ARGS=(-DGGML_CUDA=ON)
else
    echo "=== no GPU detected — CPU build ==="
fi

echo "=== python venv + package (Release) ==="
python3 -m venv .venv
# shellcheck disable=SC1091
source .venv/bin/activate
pip install --upgrade pip > /dev/null
CMAKE_ARGS="${CUDA_ARGS[*]:-}" pip install .

echo "=== Release tools tree (build-exp) ==="
cmake -B build-exp -S "$REPO" \
    -DCMAKE_BUILD_TYPE=Release \
    -DAUTOCOG_BUILD_TESTS=OFF \
    "${CUDA_ARGS[@]}"
cmake --build build-exp --target autocog_stlc autocog_ista autocog_xfta autocog_psta autocog_efta -j"$(nproc)"

echo "=== models ==="
"$SCRIPT_DIR/models.sh"

echo
echo "setup complete. next: experiments/calibrate.sh"
