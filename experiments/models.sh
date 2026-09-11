#!/usr/bin/env bash
# Fetch the benchmark models into models/.
#
# EDIT HERE before a paid run: verify each URL resolves (Hugging Face repo
# layouts move) and adjust quants to taste. Q8_0 keeps quantization noise
# out of the base-vs-instruct comparison; the tiny model is the pipeline
# smoke. Llama-3.2-1B (no suffix) is the required BASE (non-finetuned)
# datapoint.
set -euo pipefail

# shellcheck disable=SC1091
source "$(dirname "$0")/env.sh"
mkdir -p "$MODELS_DIR"
cd "$MODELS_DIR"

declare -A MODELS=(
  [tiny-llama3-test-Q2_K.gguf]="https://huggingface.co/TensorBlock/tiny-llama3-test-GGUF/resolve/main/tiny-llama3-test-Q2_K.gguf"
  [Llama-3.2-1B.Q8_0.gguf]="https://huggingface.co/QuantFactory/Llama-3.2-1B-GGUF/resolve/main/Llama-3.2-1B.Q8_0.gguf"
  [Llama-3.2-3B.Q8_0.gguf]="https://huggingface.co/QuantFactory/Llama-3.2-3B-GGUF/resolve/main/Llama-3.2-3B.Q8_0.gguf"
  [Llama-3.2-1B-Instruct-Q8_0.gguf]="https://huggingface.co/bartowski/Llama-3.2-1B-Instruct-GGUF/resolve/main/Llama-3.2-1B-Instruct-Q8_0.gguf"
  [Llama-3.2-3B-Instruct-Q8_0.gguf]="https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/main/Llama-3.2-3B-Instruct-Q8_0.gguf"
)

for name in "${!MODELS[@]}"; do
    if [ -s "$name" ]; then
        echo "have: $name"
        continue
    fi
    echo "fetching: $name"
    curl -L --fail --retry 3 -o "$name.part" "${MODELS[$name]}"
    mv "$name.part" "$name"
done

ls -lh "$MODELS_DIR"/*.gguf
