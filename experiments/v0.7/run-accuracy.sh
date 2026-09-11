#!/usr/bin/env bash
# Small accuracy campaign: base-vs-instruct x scale on the 100-question MCQ
# set, a few patterns, one unattended command after setup.sh:
#
#   experiments/v0.7/run-accuracy.sh [OUT_DIR]
#
# The product is RELATIVE accuracy (benchmarks/quality/summarize.py): how
# much each pattern extracts from a BASE model vs its instruct sibling —
# the original AutoCog question of imposing constraints on base models —
# and how the gap moves from 1B to 3B. Absent models are skipped with a
# note (models.sh fetches them; base-model URLs are the ones to verify).
#
#   QUESTIONS   questions per pattern (default 100)
#   SYNTAXES    comma list (default default,special)
#   DEMOS       comma list (default select,select-cot)
#   DATASET     builtin (default) | arc-easy | arc-challenge | mmlu —
#               the public sets need datasets/ (experiments/downloader.sh)
#               and are stratified-sampled down to QUESTIONS
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/../env.sh"

OUT="${1:-$RESULTS_DIR/accuracy-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"

QUESTIONS="${QUESTIONS:-100}"
SYNTAXES="${SYNTAXES:-default,special}"
DEMOS="${DEMOS:-select,select-cot}"

export AUTOCOG_NGL="${AUTOCOG_NGL:-99}"
# shellcheck disable=SC1091
source "$VENV/bin/activate"

MODELS=(
    "$MODELS_DIR/Llama-3.2-1B.Q8_0.gguf"
    "$MODELS_DIR/Llama-3.2-1B-Instruct-Q8_0.gguf"
    "$MODELS_DIR/Llama-3.2-3B.Q8_0.gguf"
    "$MODELS_DIR/Llama-3.2-3B-Instruct-Q8_0.gguf"
    # higher MODEL_SIZE tiers (skipped unless downloaded); Qwen pairs run
    # default syntax only — special.json is Llama-3 reserved tokens
    "$MODELS_DIR/Meta-Llama-3.1-8B.Q8_0.gguf"
    "$MODELS_DIR/Meta-Llama-3.1-8B-Instruct-Q8_0.gguf"
    "$MODELS_DIR/Qwen2.5-14B.Q8_0.gguf"
    "$MODELS_DIR/Qwen2.5-14B-Instruct-Q8_0.gguf"
    "$MODELS_DIR/Qwen2.5-32B.Q6_K.gguf"
    "$MODELS_DIR/Qwen2.5-32B-Instruct-Q6_K.gguf"
)

DATASET="${DATASET:-builtin}"
QFILE="$REPO/benchmarks/quality/questions.json"
case "$DATASET" in
    builtin) ;;
    arc-easy|arc-challenge)
        name="ARC-Easy"; [ "$DATASET" = "arc-challenge" ] && name="ARC-Challenge"
        arcfile="$(ls "$DATASETS_DIR"/ARC-V1-Feb2018*/"$name/$name-Test.jsonl" 2>/dev/null | head -1)"
        [ -n "$arcfile" ] || { echo "error: $name not found under $DATASETS_DIR — run experiments/downloader.sh --datasets-only" >&2; exit 1; }
        QFILE="$OUT/questions-$DATASET.json"
        python3 "$REPO/benchmarks/quality/convert.py" arc "$arcfile" \
            --limit "$QUESTIONS" --out "$QFILE" ;;
    mmlu)
        QFILE="$OUT/questions-mmlu.json"
        python3 "$REPO/benchmarks/quality/convert.py" mmlu \
            "$DATASETS_DIR/mmlu/test" --limit "$QUESTIONS" --out "$QFILE" ;;
    *) echo "unknown DATASET: $DATASET" >&2; exit 1 ;;
esac

{
    echo "host: $(hostname)"
    command -v nvidia-smi > /dev/null && nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv,noheader
    echo "commit: $(git -C "$REPO" rev-parse HEAD)"
    echo "ngl: $AUTOCOG_NGL  dataset: $DATASET  questions: $QUESTIONS  syntaxes: $SYNTAXES  demos: $DEMOS"
} > "$OUT/machine.txt" 2>&1 || true
cat "$OUT/machine.txt"

RESULTS=()
for model in "${MODELS[@]}"; do
    name="$(basename "$model" .gguf)"
    if [ ! -s "$model" ]; then
        echo "=== skipped (not found): $name ===" | tee -a "$OUT/accuracy.log"
        continue
    fi
    echo "=== $name ===" | tee -a "$OUT/accuracy.log"
    python3 "$REPO/benchmarks/quality/run.py" \
        --build "$BUILD_EXP" --model "$model" \
        --syntaxes "$SYNTAXES" --demos "$DEMOS" \
        --questions "$QUESTIONS" --questions-file "$QFILE" --out "$OUT/$name" \
        2>&1 | tee -a "$OUT/accuracy.log" \
        || { echo "!!! $name failed — continuing" | tee -a "$OUT/accuracy.log"; continue; }
    RESULTS+=("$OUT/$name"/*.ndjson)
done

if [ ${#RESULTS[@]} -gt 0 ]; then
    python3 "$REPO/benchmarks/quality/summarize.py" "${RESULTS[@]}" \
        --ref Llama-3.2-1B-Instruct-Q8_0 \
        | tee "$OUT/summary.md"
fi

tar -czf "$OUT/../$(basename "$OUT")-results.tar.gz" -C "$OUT/.." "$(basename "$OUT")"
echo
echo "done. pull: $OUT/../$(basename "$OUT")-results.tar.gz"
