#!/usr/bin/env bash
# Computational performance sweeps (benchmarks/compute) for the v0.7 report.
#
# Usage: run-compute.sh [OUT_DIR] [model.gguf ...]
#   Without models: every models/*.gguf except the tiny smoke model.
#   Always runs the RNG harness-overhead floor once.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT="${1:-$SCRIPT_DIR/results/$(date +%Y%m%d-%H%M%S)}"
shift $(( $# > 0 ? 1 : 0 )) || true
mkdir -p "$OUT/compute"

export AUTOCOG_NGL="${AUTOCOG_NGL:-99}"

MODELS=("$@")
if [ ${#MODELS[@]} -eq 0 ]; then
    for m in "$REPO"/models/*.gguf; do
        [[ "$m" == *tiny-llama3* ]] && continue
        MODELS+=("$m")
    done
fi

echo "=== RNG floor ==="
"$REPO/benchmarks/compute/run.sh" "$REPO/build-exp" --rng
cp "$REPO"/benchmarks/compute/results-*-rng.* "$OUT/compute/"

for model in "${MODELS[@]}"; do
    echo "=== compute sweep: $(basename "$model") (NGL=$AUTOCOG_NGL) ==="
    "$REPO/benchmarks/compute/run.sh" "$REPO/build-exp" "$model"
    tag="$(basename "${model%.gguf}")"
    cp "$REPO"/benchmarks/compute/results-*"$tag"* "$OUT/compute/" 2>/dev/null || true
done

echo "compute results: $OUT/compute/"
