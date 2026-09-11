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

# Base/instruct pairs by scale tier — MODEL_SIZE selects how far up to fetch:
#   0 (default)  tiny smoke + the Llama-3.2-1B pair          (~2.7 GB)
#   1            adds the Llama-3.2-3B and Llama-3.1-8B pairs (~24 GB more)
#   2            adds the Qwen2.5-14B and 32B pairs           (~85 GB more)
# The 32B pair is Q6_K so weights + KV fit a 40 GB A100; everything else is
# Q8_0. NOTE: share/syntax/special.json banks on Llama-3 reserved special
# tokens — run Qwen models with default syntax.
MODEL_SIZE="${MODEL_SIZE:-0}"

declare -A MODELS=(
  [tiny-llama3-test-Q2_K.gguf]="https://huggingface.co/TensorBlock/tiny-llama3-test-GGUF/resolve/main/tiny-llama3-test-Q2_K.gguf"
  [Llama-3.2-1B.Q8_0.gguf]="https://huggingface.co/QuantFactory/Llama-3.2-1B-GGUF/resolve/main/Llama-3.2-1B.Q8_0.gguf"
  [Llama-3.2-1B-Instruct-Q8_0.gguf]="https://huggingface.co/bartowski/Llama-3.2-1B-Instruct-GGUF/resolve/main/Llama-3.2-1B-Instruct-Q8_0.gguf"
)
if [ "$MODEL_SIZE" -ge 1 ]; then
  MODELS[Llama-3.2-3B.Q8_0.gguf]="https://huggingface.co/QuantFactory/Llama-3.2-3B-GGUF/resolve/main/Llama-3.2-3B.Q8_0.gguf"
  MODELS[Llama-3.2-3B-Instruct-Q8_0.gguf]="https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/main/Llama-3.2-3B-Instruct-Q8_0.gguf"
  MODELS[Meta-Llama-3.1-8B.Q8_0.gguf]="https://huggingface.co/QuantFactory/Meta-Llama-3.1-8B-GGUF/resolve/main/Meta-Llama-3.1-8B.Q8_0.gguf"
  MODELS[Meta-Llama-3.1-8B-Instruct-Q8_0.gguf]="https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/resolve/main/Meta-Llama-3.1-8B-Instruct-Q8_0.gguf"
fi
if [ "$MODEL_SIZE" -ge 2 ]; then
  MODELS[Qwen2.5-14B.Q8_0.gguf]="https://huggingface.co/QuantFactory/Qwen2.5-14B-GGUF/resolve/main/Qwen2.5-14B.Q8_0.gguf"
  MODELS[Qwen2.5-14B-Instruct-Q8_0.gguf]="https://huggingface.co/bartowski/Qwen2.5-14B-Instruct-GGUF/resolve/main/Qwen2.5-14B-Instruct-Q8_0.gguf"
  MODELS[Qwen2.5-32B.Q6_K.gguf]="https://huggingface.co/mradermacher/Qwen2.5-32B-GGUF/resolve/main/Qwen2.5-32B.Q6_K.gguf"
  MODELS[Qwen2.5-32B-Instruct-Q6_K.gguf]="https://huggingface.co/bartowski/Qwen2.5-32B-Instruct-GGUF/resolve/main/Qwen2.5-32B-Instruct-Q6_K.gguf"
fi

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
