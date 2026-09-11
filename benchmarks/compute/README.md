# benchmarks/compute — computational performance of the evaluation backend

Measures what the `xfta` backend *costs*: wall time and decoded-token
accounting across the search-parameter space, on any machine, against any
GGUF model (or the built-in RNG model for a model-free harness floor).
This is the instrument that found the branch-restore bottleneck (up to 86%
of all decoded tokens re-establishing KV prefixes) and validated the
KV-slot-pool and frontier-batching work; it stays useful as the regression
guard for any future backend change.

## The workload

`benchmark.stl` is one deliberately mixed single-prompt program that
exercises every evaluation path in one context:

- forced text scoring (header + channel-fed inputs),
- repeated bounded completions (`work[1:8] is text<length=25>`) — the
  branch/beam stress,
- one long completion (`summary`, length 50),
- choice scoring (`answer is select(choices)`).

The content is fixed (one MCQ) so runs are comparable; `--seed 42` makes
each configuration deterministic on a given build.

## Running

```bash
# Release tree is built for you (NEVER benchmark Debug/coverage: vendored
# ggml drops to -O0 and every number inflates ~100x):
benchmarks/compute/run.sh                          # RNG floor, default build dir
benchmarks/compute/run.sh build-rel models/foo.gguf
benchmarks/compute/run.sh "" models/foo.gguf --quick   # reduced matrix
```

`run.sh [BUILD_DIR] [MODEL.gguf|--rng] [--quick]` builds (ccache-warm)
Release tools into `BUILD_DIR`, then `sweep.py` compiles the workload
once, instantiates one FTA per configuration, and evaluates each with
`xfta --perf`.

Environment knobs:

- `AUTOCOG_NGL=<n>` — GPU layers to offload (default 0 = CPU; 99 = all).
  State it when reporting numbers.
- `AUTOCOG_KV_SLOTS=<n>` — KV sequence-slot pool size (default 64;
  `1` reproduces the historical single-sequence backend, which is how the
  caching layers were A/B-validated).

## The sweep matrix

Each axis stresses a different part of the machinery:

| axis    | values (full) | stresses |
|---------|---------------|----------|
| `beams` | 1, 2, 4, 8    | branch fan-out: KV ping-pong, frontier width |
| `ahead` | 1, 2, 4       | lookahead rollouts (candidate continuations scored per step) |
| `width` | 1, 2          | surviving results: FTT growth, cross-action branching |

`--quick` runs beams {1,4} x ahead {1,2} x width {1}.

Explicit cell lists replace the matrix with `sweep.py --cells FILE.json`
(each cell: `beams/ahead/width/topk/threshold/repetition/metric/slots/
stl/syntax/ctx/label`; see `experiments/v0.7/cells/`). One curated list
lives here: **`cells-bottleneck.json`**, six cells each engineered to be
dominated by a different subsystem (decode / sample / score / restore /
harness overhead / completion depth). Run it after any performance work,
plus the RNG floor: whichever column grew tells you where the next
bottleneck lives — that is how the ~10ms/token sampling sweep was found
and verified (see `benchmarks/micro/`).

## Outputs

`results-<host>-<model>.ndjson` — one ECS-flavored event per configuration
(`eval.summary` from `xfta --perf`, sweep parameters attached under
`autocog.bench.*`), SIEM-ingestable alongside the Python side's logs.
`results-<host>-<model>.md` — the same as a table. Both are gitignored;
rename with a suffix (e.g. `.baseline.`) to keep an era for comparison.

Metric glossary (the `autocog.perf.*` fields):

- `advance_seconds` — wall time inside evaluation (excludes model load).
- `tokens.restore` vs `tokens.eval` — decoded tokens split into
  *re-establishing a branch prefix* (waste, in principle) vs
  *scoring/generating* (useful work). The central diagnostic.
- `<kind>.{calls,seconds,tokens.*}` — the same, per action kind
  (`text` / `complete` / `choose`).
- `decode.{calls,seconds}` — `llama_decode` invocations and wall time:
  the model-compute term (per *call* on CPU, near-free per call on GPU).
- `sample.seconds` / `score.seconds` — CPU-side logits sweeps that do
  NOT shrink on GPU: masked top-k of completion search vs forced scoring
  of imposed tokens. The md table adds a computed `other s` column
  (`advance - decode - sample - score`): the unattributed residual
  (queue, mask building, bookkeeping). A growing residual is the next
  bottleneck announcing itself.
- `complete.tokens.lookahead` — subset of eval tokens spent on `ahead`
  rollouts.
- `kv.{exact,extends,trims,forks,evictions,tokens.primed}` — KV
  slot-pool behavior: how targets were routed (fork = prefix shared via
  `llama_memory_seq_cp`), whether the pool thrashed (evictions of live
  slots), and logits re-priming cost.
- `search.{terminals,coverage.*,stopped,abandoned}` — queue/termination
  behavior, when a stop rule is configured.

## Comparing runs

```bash
python3 compare.py old.ndjson new.ndjson
```

Config-matched table with a `!=` marker on rows whose *eval*-token counts
differ: those runs did different generation work (e.g. a scoring change
altered exploration), so their timing ratio is not a pure machinery
comparison.

```bash
python3 plot.py --out graphs --title "..." era1=a.ndjson era2=b.ndjson ...
```

Five figures. `wall.png` stars different-work configs for the same
reason. When work differs across series, **`productive.png` (ms per
productive token) is the one era-safe figure**: total cost over useful
output, so restore waste and per-call overhead both count against it.
`restore.png` shows the caching story, `pertoken.png` the batching story
(per-decoded-token cost tracks the batch size of the underlying
`llama_decode` calls), `work.png` whether runs did the same work at all.

## Interpretation notes

- On CPU, a `llama_decode` call streams the full model weights whatever
  its batch size — *calls*, not tokens, dominate. Wide search benefits
  from frontier batching; `beams=1` is a batch of one and cannot.
- The RNG floor isolates orchestration/harness overhead from model
  compute; run it once per machine.
- For cloud/GPU campaigns see `experiments/v0.7/`.
