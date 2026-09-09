# v0.7 experiments — compute analysis + MCQ benchmarking

Data-gathering runs for the v0.7 arc on a GPU cloud machine (sized for one
A100, works on any CUDA GPU or CPU-only): first the computational
performance sweep of the search parameters, then the MCQ quality benchmark
(accuracy, token overhead per syntax, constraint friction) across several
models — including at least one **base** (non-finetuned) model, which is
the before-picture for the special-token fine-tuning thesis: on a base
model the reserved structural tokens are semantically inert, so its
structural friction under `share/syntax/special.json` is the number the
fine-tune has to beat.

## Quick start (clean machine)

```bash
git clone <repo-url> autocog && cd autocog
git submodule update --init --recursive
experiments/v0.7/setup.sh          # venv + CUDA Release build + models
experiments/v0.7/run-all.sh        # compute sweeps, then quality benchmarks
```

Results land in `experiments/v0.7/results/<run-id>/` (gitignored), one
NDJSON + markdown pair per benchmark per model, plus a `results.tar.gz`
to pull off the machine.

## What the scripts do

- `setup.sh` — creates `.venv`, `pip install .` (Release; adds
  `-DGGML_CUDA=ON` when `nvidia-smi` is present), builds a separate
  Release tools tree in `build-exp/` (stlc/ista/xfta/psta/efta for the
  benchmark drivers), and downloads the models via `models.sh`.
- `models.sh` — fetches the GGUFs into `models/`. **Verify the URLs /
  quant choices before a paid run** — Hugging Face repo layouts move; the
  list at the top of the script is the single place to edit. The set:
  tiny-llama3 (pipeline smoke), Llama-3.2-1B **base**, Llama-3.2-1B
  Instruct, Llama-3.2-3B Instruct (all Q8_0).
- `run-compute.sh [model.gguf ...]` — the search-parameter sweep
  (`benchmarks/compute/run.sh`: beams x ahead x width, xfta `--perf`
  ECS events) per model, plus the RNG harness floor once.
- `run-quality.sh [model.gguf ...]` — the MCQ benchmark
  (`benchmarks/quality/run.py`: 10 ground-truth questions x 6 demos x all
  five syntaxes, `efta --score` friction) per model.
- `run-all.sh` — both, in that order, over every model in `models/`,
  collecting and tarring results.

## Notes

- **GPU offload is opt-in**: the scripts export `AUTOCOG_NGL=99` (offload
  all layers). Unset, everything runs on CPU — fine for tiny, punishing
  for 3B sweeps.
- **Never benchmark a Debug/coverage build.** The scripts only build
  Release; if you build anything by hand, keep it that way (vendored ggml
  at -O0 inflates every number ~100x).
- Expected wall time on one A100 (rough): setup ~10 min + downloads;
  compute sweep ~15-45 min per model (the width=2 / high-ahead corner
  dominates); quality ~20-60 min per model. Start with
  `run-quality.sh models/tiny-llama3-test-Q2_K.gguf` as a smoke run.
- The quality benchmark treats a (syntax, demo) pair erroring as a
  datapoint, not a failure — base models on structured syntaxes are
  expected to be rough. Accuracy for base vs instruct vs (later) tuned
  models on the same questions is the three-way comparison the plan's
  v0.7.4 milestone records.
- Determinism: seed is fixed (42) by the drivers; rerunning a cell
  reproduces it (modulo GPU kernel nondeterminism in reduction order).
