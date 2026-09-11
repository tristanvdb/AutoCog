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
cd ~/my-nfs                                  # working dir; artifacts land here
git clone --recursive -b benchmarking https://github.com/tristanvdb/AutoCog autocog
autocog/experiments/setup.sh                 # deps + venv + CUDA build + models
autocog/experiments/calibrate.sh             # rate check — paste the block back
# after retuning (budgets are env/args only):
nohup autocog/experiments/v0.7/run-perf-suite.sh 7 > perf-suite.log 2>&1 &
```

Results land in `<workdir>/results/<run-id>/`, one
NDJSON + markdown pair per benchmark per model, plus a `results.tar.gz`
to pull off the machine.

## What the scripts do

- `experiments/setup.sh` + `experiments/calibrate.sh` (one level up,
  shared by every campaign) prepare the machine and print the calibration
  block; see `experiments/README.md`.
- `run-perf-suite.sh [HOURS]` — the unattended performance suite
  (E1 core matrix, E2 mechanism ablations, E3 workload scaling on 1B;
  E4 3B anchors; E5 accuracy-budget probe), per-experiment second-budgets
  as env overrides (`E1_BUDGET`..`E4_BUDGET`, `E5_QUESTIONS`), cells
  defined in `cells/*.json`.
- `run-accuracy.sh [OUT_DIR]` — the small accuracy campaign: the
  100-question MCQ set on the base/instruct x 1B/3B Llama-3.2 four-way
  (missing models skipped with a note), patterns default,special x
  select,select-cot (`SYNTAXES`/`DEMOS`/`QUESTIONS` env overrides), then
  `benchmarks/quality/summarize.py` writes the model x pattern accuracy
  matrix and the delta-vs-instruct table to `summary.md`. The product is
  relative accuracy: what constraints extract from BASE models.
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
