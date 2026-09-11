# experiments — campaign bootstrap

Every experiment campaign on a fresh machine starts with the same two
commands, then its own (unattended) campaign script:

```bash
cd ~/my-nfs                              # your working directory (may persist)
git clone --recursive -b benchmarking https://github.com/tristanvdb/AutoCog autocog
autocog/experiments/setup.sh             # 1: deps + venv + Release builds + models
autocog/experiments/calibrate.sh         # 2: two-cell rate check — paste the block back
```

The repo stays a clean subdirectory; every artifact lands alongside it:

```
~/my-nfs/
  autocog/                     the checkout
  .venv/  build-exp/  models/  results/  .ccache/
```

That resolution lives in `env.sh` (sourced by every script): the working
directory is wherever you invoke from — invoking from *inside* the repo
falls back to the old self-contained layout — with `AUTOCOG_WORKDIR`
overriding everything and `MODELS_PATH` just the models directory.

- `setup.sh` — installs missing system dependencies (toolchain, cmake,
  curl, python3-venv; `apt`/`dnf` auto-detected, `sudo` only when not
  root; skipped entirely when everything is present), then creates
  `.venv` and `pip install .` (Release; adds `-DGGML_CUDA=ON` when
  `nvidia-smi` is present), builds the Release CLI tools tree in
  `build-exp/`, and fetches models + datasets via `downloader.sh`. The CUDA
  *toolkit* is never auto-installed — use a CUDA image; a GPU-without-
  nvcc situation is diagnosed with a warning. **Safe to re-run, and
  networked-FS aware**: models and the repo-local ccache (`.ccache/`)
  persist as wins across machines, while a stale `.venv` or `build-exp/`
  (interpreter, toolchain, or CUDA state changed since the stamp) is
  detected and rebuilt rather than trusted.
- `downloader.sh` — everything a campaign fetches, runnable standalone
  and skip-if-present (pre-placing files by hand works): models into
  `models/` via `models.sh`, plus the accuracy datasets into `datasets/`
  — ARC Easy+Challenge (`ai2-public-datasets.s3.amazonaws.com/arc/
  ARC-V1-Feb2018.zip`) and MMLU (`people.eecs.berkeley.edu/~hendrycks/
  data.tar`), both canonical no-auth distributions.
- `models.sh` — the model list is the single edit point; **verify the
  Hugging Face URLs before a paid run**. Ships: tiny-llama3 (smoke),
  Llama-3.2-1B/3B base (the non-finetuned datapoints), 1B/3B Instruct
  (Q8_0). `MODELS_BIG=1` adds the Llama-3.1-8B, Qwen2.5-14B, and
  Qwen2.5-32B base+instruct pairs (~90 GB; 32B at Q6_K to fit a 40 GB
  A100 with KV headroom).
- `calibrate.sh` — one narrow and one wide cell on the 1B model
  (~3-6 min): fits per-token/per-call rates, proves GPU offload
  (`AUTOCOG_NGL=99` by default), and prints a `CALIBRATION` block. Paste
  it back to retune the campaign's budgets — retuning is arguments/env
  only, never a code edit. `MODEL=...` overrides the model.

Campaigns live in subdirectories with their own README and scripts
(budgeted, resumable, results gitignored):

- `v0.7/` — performance suite (`run-perf-suite.sh`) and MCQ quality
  benchmarking (`run-quality.sh`, `run-all.sh`).
