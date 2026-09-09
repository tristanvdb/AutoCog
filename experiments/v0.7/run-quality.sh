#!/usr/bin/env bash
# MCQ quality benchmarks (benchmarks/quality) for the v0.7 report:
# accuracy, token overhead, and constraint friction per syntax, per model.
# The base (non-finetuned) model's run is the fine-tuning before-picture.
#
# Usage: run-quality.sh [OUT_DIR] [model.gguf ...]
#   Without models: every models/*.gguf except the tiny smoke model.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT="${1:-$SCRIPT_DIR/results/$(date +%Y%m%d-%H%M%S)}"
shift $(( $# > 0 ? 1 : 0 )) || true
mkdir -p "$OUT/quality"

export AUTOCOG_NGL="${AUTOCOG_NGL:-99}"
# shellcheck disable=SC1091
source "$REPO/.venv/bin/activate"

MODELS=("$@")
if [ ${#MODELS[@]} -eq 0 ]; then
    for m in "$REPO"/models/*.gguf; do
        [[ "$m" == *tiny-llama3* ]] && continue
        MODELS+=("$m")
    done
fi

for model in "${MODELS[@]}"; do
    echo "=== quality benchmark: $(basename "$model") (NGL=$AUTOCOG_NGL) ==="
    python3 "$REPO/benchmarks/quality/run.py" \
        --build "$REPO/build-exp" \
        --model "$model" \
        --out "$OUT/quality"
done

echo "quality results: $OUT/quality/"
