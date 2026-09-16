# experiments — designing and running a campaign

A **campaign** is a descriptor: a JSON file saying what to measure. It is
data, owned by whoever runs the experiment, and it does not live in this
repo. The three scripts here are the whole harness — they take descriptors
and do everything else.

```bash
cd ~/my-nfs                                       # the workdir
git clone --recursive -b benchmarking https://github.com/tristanvdb/AutoCog autocog

autocog/share/experiments/setup.sh campaigns/*.json     # 1. build + fetch what they name
autocog/share/experiments/calibrate.sh                  # 2. verify machine + features
autocog/share/experiments/campaign.sh campaigns/*.json  # 3. run them
```

Everything lands beside the checkout (`env.sh` resolves this; invoking from
inside the repo falls back to a self-contained layout, and
`AUTOCOG_WORKDIR` overrides):

```
~/my-nfs/
  autocog/   campaigns/   .venv/   models/   datasets/   results/   .ccache/
```

Retuning a campaign is editing its descriptor — never a code edit. If you
find yourself writing a script beside these three, the descriptor format or
one of these scripts is missing something: extend it there.

---

## 1. The descriptor

```json
{
  "name": "c3-label-small",
  "defaults": { "seed": 42 },
  "worker":   { "mode": "distributed", "factor": 8 },
  "models":   [ {"model": "Llama-3.2-1B"}, {"model": "Llama-3.2-3B-Instruct"} ],
  "datasets": [ "arc-easy", "mmlu-1000" ],
  "axes":     [ "model", "syntax", "demo" ],
  "phases": [
    { "name": "label",
      "runs": [
        { "kind": "quality", "model": "Llama-3.2-1B", "data": "arc-easy",
          "syntaxes": ["complete", "stripped"], "demos": ["label"],
          "out": "lab-1b-easy" }
      ] }
  ]
}
```

| field | meaning |
|---|---|
| `name` | campaign id; results land in `results/<name>/`, archived as `results/<name>-<stamp>.tar.gz` |
| `defaults` | merged into every run (typically `seed`) |
| `worker` | how to spread the models over this machine — §3 |
| `models` | every model tag the runs use. `setup.sh` fetches exactly these; the launcher verifies them and assigns them to workers. Load params ride along: `{"model": "...", "ctx": 8192, "ngl": 99, "kv_slots": 16}` |
| `datasets` | every dataset name the runs use. `setup.sh` produces them (§4); the launcher refuses to start if one is missing |
| `axes` | fields the monitor slices live accuracy by (default `model`, `syntax`, `demo`) |
| `phases` | ordered, each a barrier: a phase completes before the next starts |

**Name a campaign after the campaign, not the software.** They are numbered
by sequence (`c3-...` is the third); release numbers have nothing to do
with it.

### Run kinds

**`quality`** — accuracy, token overhead, friction over a question set.
One run expands to `questions × syntaxes × demos` samples.

| field | default | notes |
|---|---|---|
| `model` | `rng` | a model *tag*, not a path; some worker must host it |
| `data` | — | dataset name (§4) |
| `questions` | all | cap, taken from the head of the file |
| `syntaxes` | `complete,stripped` | scaffold renderings: `complete`, `indent-index`, `indent`, `stripped`, `chatml`, `llama2chat`, `llama3chat`, `special` |
| `demos` | `select` | answer mechanisms: `select` (emit the choice index), `repeat` (score the choice texts), `label` (emit the choice letter); each also as `-cot` (free-text `work` field first), `-hyp` (two-stage), `-iter` (retry loop). `-annot` is currently broken |
| `formatter` | — | `file.py:fn` reshaping the question records |
| `out` | `<phase>-<i>` | subdirectory under `results/<name>/` |

**`perf`** — cost per configuration, no accuracy. Perf runs take an
exclusive worker and execute before the phase's quality work, so timing
never shares the machine.

| field | notes |
|---|---|
| `cells` | a cell file under `share/benchmarks/compute/`: `termination`, `bottleneck` |
| `budget_seconds` | per-run cap |
| `tag` | label carried into the events |

**`probe`** — records every candidate branch with per-token
log-probabilities, so ranking rules are adjudicated offline from one pass.
Executed by the launcher rather than `autocog bench`:
`{"kind":"probe","model":...,"demo":"repeat","syntax":"complete","data":"arc-easy","limit":1000}`.
A phase may not mix probe and non-probe runs. Without `limit`, the launcher
applies `--probe-limit` (default 1000) and logs that it did.

---

## 2. Designing one

**Vary one thing.** The reason the descriptor separates content, scaffold,
mechanism and scoring rule is that a run changing two at once measures
neither. For a paired comparison, give both arms the same `data` and
`questions` so items line up item-by-item.

**Order runs by what you most need.** Each campaign is a transaction:
verify, spawn workers, run, archive, tear down, next. Put the arm your
conclusion depends on in the first descriptor, so a box that dies at hour
three still produced it.

**Size it from measured throughput.** Per-lane throughput is in the last
campaign's events (the monitor prints it live; `wall_seconds` on each
result row is the per-sample cost):

```
wall ≈ (runs × questions × syntaxes × demos) ÷ (lanes × evals-per-second-per-lane)
```

Check the extremes before trusting it: `-cot` and `-hyp` samples cost
5–20× a plain `select`, since they generate free text before answering.

**Then cut to fit the box.** Dropping a syntax halves a grid faster than
dropping a model, and usually costs less science — syntaxes are often
correlated, while models are the axis under study.

---

## 3. Workers: modes, lanes, VRAM

The descriptor declares a *mode*; the launcher adapts it to the machine.
A descriptor never mentions GPUs.

- `{"mode": "isolated"}` — one worker hosting every model. Use for perf
  work: timing must not share the card.
- `{"mode": "distributed", "factor": N}` — `GPUs × N` workers, models
  round-robin. With `factor` above the model count, models are
  **replicated**, giving each more lanes (weights duplicated). Fails fast
  if models outnumber slots.

A worker is one execution **lane** — one evaluation at a time — so lanes
set wall-clock. Size `factor` by VRAM: total weights ≈ workers × model
size, plus KV headroom. Four 8B Q8_0 workers do not fit a 40 GB card; two
do. Cores are split automatically (`AUTOCOG_THREADS = nproc / workers`),
overridable by exporting it.

---

## 4. What setup.sh does with your descriptors

- **Models**: `models.sh` fetches exactly the tags the descriptors name;
  its catalog is the single edit point — **verify the Hugging Face URLs
  before a paid run**. With no descriptors it falls back to the
  `MODEL_SIZE` tier: `0` tiny + 1B pair, `1` adds 3B and 8B, `2` adds
  14B/32B.
- **Datasets**: the canonical no-auth distributions (ARC, MMLU) download
  once, then every name in a descriptor's `datasets` is produced from
  them, source and sample size read off the name — `arc-easy`,
  `arc-challenge`, `arc-challenge-500`, `mmlu-1000`, `arc-challenge-250`.
  A name matching no rule warns; the launcher then refuses to start.

Everything is skip-if-present, so re-running costs nothing and pre-placing
files by hand works.

`calibrate.sh` checks the machine once per hardware configuration (profile
cached in `.calibration/`): that the installed package has a GPU backend if
the box has GPUs, that the build is `Release`, that every answer mechanism
produces scored samples on this build, and what the per-token and per-call
rates are.

---

## 5. Running, watching, resuming

```bash
campaign.sh --dryrun campaigns/c3-*.json    # plan only: workers, routing, outputs
campaign.sh campaigns/c3-label-small.json campaigns/c3-mmlu-small.json
```

Options: `--ngl N` (GPU layers), `--phase P` (restrict, repeatable),
`--probe-limit N` (default probe question cap, `0` = full dataset).

Run it in the foreground. From a second terminal:

```bash
autocog/share/experiments/monitor.sh        # live; --once for a snapshot
```

The monitor tails `results/campaign-events.ndjson`: campaigns done/running/
pending, progress and ETA, accuracy sliced by the descriptor's `axes`, and
per-worker throughput and **occupancy** polled from each worker's `/stats`.
Occupancy is the number to watch — 80–100% means the lanes are working;
low occupancy with an empty queue means the client is the bottleneck.

**Resume is per sample.** A quality run indexes the samples already
recorded and executes only the gaps, so an interrupted campaign restarts by
rerunning the identical command: nothing measured is repeated or lost. Perf
runs resume per run.

Results are `results/<name>/<out>/results-*.ndjson`, one event per sample,
with a markdown summary beside them.
