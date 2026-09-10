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
