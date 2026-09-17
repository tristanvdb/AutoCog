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

## The validity gate

Before the matrix runs, a few real samples are checked for answers that are
*usable*: in range, and not identical across every question. A campaign that
fails this stops having spent nothing.

It exists because the previous gate — "samples came back, no errors" — passed
a run in which 17% of answers were letters naming no choice, every one of them
the same letter. An unusable answer is still *scored* (as a miss), so
error-freedom was never the property worth asserting. `--skip-gate` runs
anyway, and you should be able to say why.

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
