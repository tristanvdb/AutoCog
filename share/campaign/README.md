# Campaigns

```bash
cd ~/my-nfs && autocog/share/campaign.sh my-campaign.yml
```

That is the whole interface. Setup is part of it — the venv, the build, the
model and dataset downloads all happen on the way, all idempotent, so there
is nothing to run first and no order to remember. Re-running after a crash
picks up where it stopped.

A descriptor is data, owned by whoever runs the experiment; it does not live
in this repo. Artifacts land **beside** the checkout, never inside it, and
nothing is hidden:

```
~/my-nfs/
  autocog/          the checkout
  venv/  build/  ccache/
  models/  datasets/  calibration/
  results/<descriptor basename>/
```

Watch it from a second terminal:

```bash
python3 autocog/share/campaign/monitor.py results/campaign-events.ndjson
```

## The descriptor

YAML if PyYAML is installed, JSON otherwise; the extension is never consulted.
The campaign is named after the file, so there is no `name:` field to
disagree with it.

```yaml
models:
  - {tag: 1b,  file: Llama-3.2-1B-Instruct-Q8_0.gguf, ctx: 4096}
  - {tag: 8b,  file: Llama-3.1-8B-Instruct-Q8_0.gguf, ctx: 4096}

datasets:
  - {name: arc-1000,  source: arc,  split: test, limit: 1000}
  - {name: mmlu-1000, source: mmlu, split: test, limit: 1000}

programs:
  strings-select:
    filepath: demos/mcq/strings-select.stl
    options: []
    labels: [select, strings]
  record-label:
    filepath: demos/mcq/record-label.stl
    labels: [repeat, record, label]

defaults:
  syntaxes: [complete, special, stripped]
  samples: 200

runs:
  - {name: mechanism-arc, model: 1b, dataset: arc-1000,
     programs: [strings-select, record-label]}
  - {name: mechanism-8b,  model: 8b, dataset: arc-1000,
     programs: [strings-select, record-label]}
```

**`models`** — `tag` is how a run names one, `file` is what to fetch (see
`catalog.py`, and check it before a paid run: Hugging Face layouts move). The
reserved tag `rng` is the built-in pseudo-model: no weights, served by every
worker, which proves a campaign's plumbing for free.

**`datasets`** — `name` is what a run asks for and what the built file is
called; `source`/`split` say how to build it when it is missing. Conversion is
where choice labels come from, decided once.

**`programs`** — `filepath` absolute or relative to `share/`; `options` are the
compiler's own flags (`-I dir`, `-D name=value`, `-e entry`); `labels` are
free-form tags, opaque to everything, riding on every event so the analysis
can group by them.

**`runs`** — an ordered list, not a matrix. `defaults` fills in what a run
does not say.

**`prior`** — earlier campaigns this one may reuse. See below.

### Labels are how you ask a question

Nothing parses a program's name. `record-index` and `record-value` sharing
`record` and differing on `select`/`repeat` is what turns "does the mechanism
or the shape matter more?" into a grouping rather than an analysis script.
Label for the contrast you intend to draw; the monitor slices by it live.

## What the descriptor does NOT say

Anything about the machine. Worker count, which GPU holds which model, how
many lanes saturate it — all measured, so a descriptor moves between boxes
unedited. `--mode isolated|distributed` overrides the topology when debugging;
`--dry-run` prints the plan and spawns nothing.

## Paying once: `prior`

Re-running a campaign into the same `results/<name>/` already skips the samples
that are there. `prior` is for everything else — the campaign was renamed, the
results were archived, or a new campaign overlaps an old one:

```yaml
prior: [screen-base-1b, ../old-box/results/screen-base-1b]
```

Each entry is a campaign name under `results/`, or a path; both are scanned
recursively, so run labels and phase layout need not match. A name that
resolves to nothing is **fatal** — a typo that quietly degraded into "reuse
nothing" would fail in the only way you cannot see, by just running and
costing what it was supposed to save. `--prior` adds more from the command
line.

An adopted sample is **copied** into this campaign's results, not referenced,
so a tarball of `results/<name>/` is still the whole campaign. It keeps its
original `@timestamp` and carries `autocog.bench.reused.from`.

### What makes two samples the same sample

Not `(syntax, program, question)` — that names a *cell*, and the same cell
measures something else the moment a syntax file is edited. Identity is
`autocog.bench.fingerprint`, a digest of the model tag, the syntax and search
JSON **content**, the compiled program's own STA hash, and the question's
content. Reindent a syntax file and reuse still applies; change a field and
every sample under it is re-measured, in the prior campaign *and* in this
campaign's own directory.

Two things the digest does not cover, because a hash invites more trust than
it earns:

- **The model tag is a name, not a checksum of the weights.** Pointing
  `Llama-3.2-1B` at a different GGUF matches and measures something else. Tag
  discipline is yours; a descriptor never sees a path.
- **The KV slot count.** Any two counts disagree slightly on free-text
  logprobs while agreeing on tree shape, so it is an experimental variable —
  but it is measured per box, not declared, and keying on it would void every
  reuse whenever the worker layout changed. It rides on the event instead;
  group by it in analysis.

Results written before fingerprints existed keep resuming on the triple: they
are already collected, and invalidating them buys a guarantee about the past.

## Two gates

**Preflight**, before provisioning: every `(program, syntax)` arm in the
descriptor is executed once on `rng`. No weights, no GPU, ~50 arms/second, so a
descriptor that cannot execute fails before a byte of model is downloaded.

It exists because a 332-arm descriptor generated from the syntax grid had 42
arms raising `IndexError` out of the program's own resolver, and the only gate
that ran would have passed every time — it touched one arm.

**The validity gate**, once the workers are up: a few real samples are checked
for answers that are *usable*: in range, and not identical across every
question. A campaign that fails this stops having spent nothing.

It exists because the previous gate — "samples came back, no errors" — passed
a run in which 17% of answers were letters naming no choice, every one of them
the same letter. An unusable answer is still *scored* (as a miss), so
error-freedom was never the property worth asserting.

`--skip-gate` skips both, and you should be able to say why.

Neither can see an arm that runs and returns the *wrong* answer: `rng` answers
are noise, so "plausible" is all a check without a model can mean, and a
resolver that is off by one produces a perfectly plausible choice. Read the
graphs (`ctest -L inspect`).

## Designing one

- **Vary one thing per contrast.** Two programs differing only in mechanism
  tell you about mechanism; two differing in mechanism *and* shape tell you
  nothing.
- **Order runs so an early death still pays.** The list executes in order —
  put the arm you most need first.
- **Size from measured throughput**, not from hope: `calibration/` holds what
  the last probe found.
- **Reasoning scaffolds cost 5–20x a bare mechanism.** A `-cot` arm across
  three syntaxes and two models is not a small addition.
- **Cut a syntax before cutting a model.** A syntax is cheap to add back; a
  model arm that never ran is a hole in every comparison.

## Not here: performance

Quality runs want every lane busy on whatever machine was cheapest today. Perf
runs want an idle machine you control. They do not belong in one descriptor —
see `share/perf/`.
