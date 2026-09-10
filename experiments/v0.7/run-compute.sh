#!/usr/bin/env bash
# Computational performance sweeps (benchmarks/compute) for the v0.7 report.
#
# Usage: run-compute.sh [OUT_DIR] [model.gguf ...]
#   Without models: every $MODELS_DIR/*.gguf except the tiny smoke model.
#   Always runs the RNG harness-overhead floor once.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/../env.sh"
OUT="${1:-$RESULTS_DIR/$(date +%Y%m%d-%H%M%S)}"
shift $(( $# > 0 ? 1 : 0 )) || true
mkdir -p "$OUT/compute"

export AUTOCOG_NGL="${AUTOCOG_NGL:-99}"

MODELS=("$@")
if [ ${#MODELS[@]} -eq 0 ]; then
    for m in "$MODELS_DIR"/*.gguf; do
        [[ "$m" == *tiny-llama3* ]] && continue
        MODELS+=("$m")
    done
fi

echo "=== RNG floor ==="
"$REPO/benchmarks/compute/run.sh" "$BUILD_EXP" --rng --out "$OUT/compute"

for model in "${MODELS[@]}"; do
    echo "=== compute sweep: $(basename "$model") (NGL=$AUTOCOG_NGL) ==="
    "$REPO/benchmarks/compute/run.sh" "$BUILD_EXP" "$model" --out "$OUT/compute"
done

echo "compute results: $OUT/compute/"
