#!/usr/bin/env bash
# MCQ quality benchmarks (benchmarks/quality) for the v0.7 report:
# accuracy, token overhead, and constraint friction per syntax, per model.
# The base (non-finetuned) model's run is the fine-tuning before-picture.
#
# Usage: run-quality.sh [OUT_DIR] [model.gguf ...]
#   Without models: every models/*.gguf except the tiny smoke model.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/../env.sh"
OUT="${1:-$RESULTS_DIR/$(date +%Y%m%d-%H%M%S)}"
shift $(( $# > 0 ? 1 : 0 )) || true
mkdir -p "$OUT/quality"

export AUTOCOG_NGL="${AUTOCOG_NGL:-99}"
# shellcheck disable=SC1091
source "$VENV/bin/activate"

MODELS=("$@")
if [ ${#MODELS[@]} -eq 0 ]; then
    for m in "$MODELS_DIR"/*.gguf; do
        [[ "$m" == *tiny-llama3* ]] && continue
        MODELS+=("$m")
    done
fi

for model in "${MODELS[@]}"; do
    echo "=== quality benchmark: $(basename "$model") (NGL=$AUTOCOG_NGL) ==="
    python3 "$REPO/benchmarks/quality/run.py" \
        --build "$BUILD_EXP" \
        --model "$model" \
        --out "$OUT/quality"
done

echo "quality results: $OUT/quality/"
