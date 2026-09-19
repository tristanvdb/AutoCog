"""Where the benchmark models come from.

EDIT HERE before a paid run: verify each URL still resolves (Hugging Face repo
layouts move) and adjust quants to taste. Q8_0 keeps quantization noise out of
the base-vs-instruct comparison; the tiny model is the pipeline smoke. The
no-suffix names are the BASE (non-finetuned) models, which is the
before-picture for anything about fine-tuning.

Files are stored under tag-prefixed names so a descriptor's `file` reads the
same whatever the upstream repo calls it.

NOTE: share/syntax/special.json banks on Llama-3 reserved special tokens —
run Qwen models with a default syntax.
"""

MODELS = {
    "tiny-llama3-test-Q2_K.gguf":
        "https://huggingface.co/TensorBlock/tiny-llama3-test-GGUF/resolve/main/tiny-llama3-test-Q2_K.gguf",

    "Llama-3.2-1B.Q8_0.gguf":
        "https://huggingface.co/QuantFactory/Llama-3.2-1B-GGUF/resolve/main/Llama-3.2-1B.Q8_0.gguf",
    "Llama-3.2-1B-Instruct-Q8_0.gguf":
        "https://huggingface.co/bartowski/Llama-3.2-1B-Instruct-GGUF/resolve/main/Llama-3.2-1B-Instruct-Q8_0.gguf",

    "Llama-3.2-3B.Q8_0.gguf":
        "https://huggingface.co/QuantFactory/Llama-3.2-3B-GGUF/resolve/main/Llama-3.2-3B.Q8_0.gguf",
    "Llama-3.2-3B-Instruct-Q8_0.gguf":
        "https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/main/Llama-3.2-3B-Instruct-Q8_0.gguf",

    "Llama-3.1-8B.Q8_0.gguf":
        "https://huggingface.co/QuantFactory/Meta-Llama-3.1-8B-GGUF/resolve/main/Meta-Llama-3.1-8B.Q8_0.gguf",
    "Llama-3.1-8B-Instruct-Q8_0.gguf":
        "https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/resolve/main/Meta-Llama-3.1-8B-Instruct-Q8_0.gguf",

    "Qwen2.5-14B.Q8_0.gguf":
        "https://huggingface.co/QuantFactory/Qwen2.5-14B-GGUF/resolve/main/Qwen2.5-14B.Q8_0.gguf",
    "Qwen2.5-14B-Instruct-Q8_0.gguf":
        "https://huggingface.co/bartowski/Qwen2.5-14B-Instruct-GGUF/resolve/main/Qwen2.5-14B-Instruct-Q8_0.gguf",
    # Q6_K so weights + KV fit a 40 GB A100
    "Qwen2.5-32B.Q6_K.gguf":
        "https://huggingface.co/mradermacher/Qwen2.5-32B-GGUF/resolve/main/Qwen2.5-32B.Q6_K.gguf",
    "Qwen2.5-32B-Instruct-Q6_K.gguf":
        "https://huggingface.co/bartowski/Qwen2.5-32B-Instruct-GGUF/resolve/main/Qwen2.5-32B-Instruct-Q6_K.gguf",
}


def url_for(filename):
    return MODELS.get(filename)
