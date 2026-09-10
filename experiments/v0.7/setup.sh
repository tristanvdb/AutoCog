#!/usr/bin/env bash
# One-time machine setup: python env + Release builds + models.
# Assumes a clean clone with submodules initialized:
#   git submodule update --init --recursive
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"
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

echo "=== calibration (E0): quick matrix on RNG + 1B ==="
export AUTOCOG_NGL="${AUTOCOG_NGL:-99}"
CAL="$SCRIPT_DIR/results/calibration"
mkdir -p "$CAL"
python3 "$REPO/benchmarks/compute/sweep.py" --build "$REPO/build-exp" --rng \
    --quick --tag calib --out "$CAL"
python3 "$REPO/benchmarks/compute/sweep.py" --build "$REPO/build-exp" \
    --model "$REPO/models/Llama-3.2-1B-Instruct-Q8_0.gguf" \
    --quick --tag calib --out "$CAL"

echo
echo "=================== CALIBRATION — paste this block back ==================="
{
    command -v nvidia-smi > /dev/null && nvidia-smi --query-gpu=name,memory.total --format=csv,noheader
    python3 - "$CAL" <<'PYEOF'
import glob, json, sys
for path in sorted(glob.glob(sys.argv[1] + "/results-*calib.ndjson")):
    for line in open(path):
        r = json.loads(line)
        tok = r["autocog.perf.tokens.restore"] + r["autocog.perf.tokens.eval"]
        calls = r.get("autocog.perf.decode.calls", 0)
        print(f"{r['autocog.bench.model']:>28} "
              f"b{r['autocog.bench.beams']}a{r['autocog.bench.ahead']}w{r['autocog.bench.width']}: "
              f"eval={r['autocog.perf.advance_seconds']:.2f}s wall={r['autocog.bench.wall_seconds']:.2f}s "
              f"tok={tok} calls={calls} "
              f"decode={r.get('autocog.perf.decode.seconds', 0):.2f}s "
              f"sample={r.get('autocog.perf.sample.seconds', 0):.2f}s")
PYEOF
} 2>&1
echo "==========================================================================="
echo
echo "setup complete. next (unattended, ~7h default):"
echo "  nohup experiments/v0.7/run-perf-suite.sh 7 > perf-suite.log 2>&1 &"
