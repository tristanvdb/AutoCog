# experiments — campaign bootstrap

Every experiment campaign on a fresh machine starts with the same two
commands, then its own (unattended) campaign script:

```bash
git clone <repo-url> autocog && cd autocog
git submodule update --init --recursive
experiments/setup.sh          # 1: venv + Release builds (CUDA if present) + models
experiments/calibrate.sh      # 2: two-cell rate check — paste the printed block back
```

- `setup.sh` — installs missing system dependencies (toolchain, cmake,
  curl, python3-venv; `apt`/`dnf` auto-detected, `sudo` only when not
  root; skipped entirely when everything is present), then creates
  `.venv` and `pip install .` (Release; adds `-DGGML_CUDA=ON` when
  `nvidia-smi` is present), builds the Release CLI tools tree in
  `build-exp/`, and downloads the models via `models.sh`. The CUDA
  *toolkit* is never auto-installed — use a CUDA image; a GPU-without-
  nvcc situation is diagnosed with a warning.
- `models.sh` — the model list is the single edit point; **verify the
  Hugging Face URLs before a paid run**. Ships: tiny-llama3 (smoke),
  Llama-3.2-1B base (the non-finetuned datapoint), 1B/3B Instruct (Q8_0).
- `calibrate.sh` — one narrow and one wide cell on the 1B model
  (~3-6 min): fits per-token/per-call rates, proves GPU offload
  (`AUTOCOG_NGL=99` by default), and prints a `CALIBRATION` block. Paste
  it back to retune the campaign's budgets — retuning is arguments/env
  only, never a code edit. `MODEL=...` overrides the model.

Campaigns live in subdirectories with their own README and scripts
(budgeted, resumable, results gitignored):

- `v0.7/` — performance suite (`run-perf-suite.sh`) and MCQ quality
  benchmarking (`run-quality.sh`, `run-all.sh`).
