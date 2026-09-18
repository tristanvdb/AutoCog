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
  - {name: arc-1000,  source: arc,  split: test, limit: 1000, samples: 200}
  - {name: mmlu-1000, source: mmlu, split: test, limit: 1000, samples: 200}

programs:
  strings-select:
    filepath: demos/mcq/strings-select.stl
    options: []
    labels: [select, strings]
  record-label:
    filepath: demos/mcq/record-label.stl
    labels: [repeat, record, label]

syntaxes:
  i1-tab:                        # complete.json with two knobs changed
    filepath: complete.json
    labels: [base1, indent-tab]
    prompt_zero_index: false
    prompt_indent: "\t"

program_groups:
  mechanism: [strings-select, record-label]
syntax_groups:
  index: [complete, i1-tab]

runs:
  - {name: mechanism-arc, model: 1b, dataset: arc-1000,
     programs: ["@mechanism"], syntaxes: ["@index"]}
  - {name: mechanism-8b,  model: 8b, dataset: arc-1000,
     programs: ["@mechanism"], syntaxes: ["@index"]}
```

**`models`** — `tag` is how a run names one, `file` is what to fetch (see
`catalog.py`, and check it before a paid run: Hugging Face layouts move). The
reserved tag `rng` is the built-in pseudo-model: no weights, served by every
worker, which proves a campaign's plumbing for free.

**`datasets`** — `name` is what a run asks for and what the built file is
called; `source`/`split` say how to build it when it is missing. Conversion is
where choice labels come from, decided once. **`samples`** is how many
questions the campaign draws from it (0 = all) — see below.

**`programs`** — `filepath` absolute or relative to `share/`; `options` are the
compiler's own flags (`-I dir`, `-D name=value`, `-e entry`); `labels` are
free-form tags, opaque to everything, riding on every event so the analysis
can group by them.

**`syntaxes`** — a base file plus what you change. See below.

**`program_groups` / `syntax_groups`** — a name for a list, used as `"@name"`
in a run (the `@` needs the quotes). The same list is usually shared by
several runs, and a list repeated is a list that drifts: two runs meant to be
one contrast quietly stop being one when somebody edits a copy. Groups do not
nest — a group of groups reads like an abstraction and behaves like an
indirection nobody can follow when a run turns out to have the wrong arms.

**`runs`** — an ordered list, not a matrix. `defaults` fills in what a run
does not say.

**`prior`** — earlier campaigns whose questions this one must not draw
again. See below.

### Syntaxes are a base plus overrides

A syntax is a flat JSON of rendering knobs, and an experiment usually varies
two or three of them. Shipping a file per combination does not scale — four
knobs over one base is 64 near-identical files, a generated artifact
pretending to be source. So name a base and state what changes:

```yaml
syntaxes:
  i1-tab:
    filepath: complete.json
    labels: [base1, indent-tab]
    prompt_zero_index: false
    prompt_indent: "\t"
```

`filepath` resolves **beside the descriptor first**, then in `share/syntax`,
so your own file wins over a shipped one of the same name. It defaults to
`<name>.json`, which is why `syntaxes: [complete]` works with nothing
declared. Every other key overrides the loaded file, and overriding a field
the base does not have is an **error** — a mistyped knob is otherwise a no-op
that reads as an arm: the campaign runs, two cells render identically, and
the contrast you wrote it for never happened.

`filepath` and `labels` describe the *arm*, not the rendering, and are
stripped before the file is written — a stray key in a syntax is a field the
C++ codec does not know, and the campaign would die on the box long after
this was the cheap thing to catch.

The resolved syntaxes are written to `results/<campaign>/syntax/<name>.json`
and the runs point at those. The name is not a path; it is the string in
`autocog.bench.syntax` on every result row, which is what an analysis groups
by.

### Syntax labels

`labels` is to a syntax what it is to a program, and lands in its own event
key, `autocog.bench.syntax.labels`. Never merged with the program's: `index`
is already a program label on `record-index` *and* the obvious name for the
index knob, and one list would make "group by `index`" silently mix a
mechanism with a rendering.

Every knob a spec **names** also gets a `knob=value` label automatically
(`prompt_zero_index=false`, `prompt_indent="\t"` — JSON spelling, so a label
never holds a raw tab). Named, not "differs from the base": a cell whose value
happens to match the base is still part of that contrast and must not drop out
of the slice. So the written labels are free to be the human names and the
derived ones cannot drift from what actually rendered.

This is what turns the question into a groupby. "Are the one-based arms
worse?" is one slice over `base1`, not a regex across syntax names — the same
reason nothing parses a program's name.

`python3 share/syntax/dedup.py <descriptor>` reports which of a campaign's
syntaxes actually render differently, per program — a knob a program's schema
never exercises renders nothing, so `prompt_indent` is inert on a flat schema
and `prompt_zero_index` is inert under `repeat`. Over the nine MCQ demos and a
64-cell grid that is 332 distinct arms out of 576. It is model-free and takes
seconds; run it before paying for the duplicates.

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

## The campaign's questions

A campaign draws `samples` questions per dataset **once, before any run**, and
writes them out:

```
results/<campaign>/samples-<dataset>.json
```

Every run is then pointed at that file. This is why `samples` sits on the
dataset and not on a run: arms are only comparable if they were asked the same
questions, and a per-run draw makes "syntax A beat syntax B" indistinguishable
from "syntax A got the easier questions". A run that sets `samples` is a
descriptor error.

The selection is a real dataset file — readable, diffable, and the thing you
hand someone who asks what the campaign actually measured. It is written once:
a campaign that died halfway does not re-draw when it resumes.

### `prior`: two campaigns, no intersection

```yaml
prior: [screen-base-1b, ../old-box/results/screen-base-1b]
```

The questions those campaigns used are subtracted from the pool **before** the
draw, so this campaign still gets its full `samples` — of questions nobody has
spent on yet. Two campaigns over one dataset therefore have disjoint sample
sets, and their results can be concatenated without counting anything twice.
Nothing is ever copied forward; a row sitting in two campaigns is a row every
consumer double-counts.

Run the same descriptor twice with `prior` and you get 2N, not N re-measured.

Each entry is a campaign name under `results/`, or a path; both are scanned
recursively (`samples-*.json` for what a campaign committed to, result rows as
the backstop), so run labels and layout need not match. A name that resolves
to nothing is **fatal** — a typo degrading into "avoid nothing" fails in the
only way you cannot see, by running and silently overlapping. If the pool
cannot supply `samples` unused questions, that is fatal too: quietly running
12 where the descriptor said 100 looks exactly like running 100 until somebody
reads the error bars.

A question is spent even if its sample errored. A resume re-runs a failed
sample because it is not a measurement; a later campaign still avoids the
question, because drawing it again is an intersection.

### Identity within a campaign

Resume compares `autocog.bench.fingerprint` — the model tag, the syntax and
search JSON **content**, the compiled program's own STA hash, and the
question's content. Not `(syntax, program, question)`, which names a *cell*:
edit a syntax file and the same cell measures something else, and the old
numbers used to survive the re-run.

Two things the digest does not cover, because a hash invites more trust than
it earns:

- **The model tag is a name, not a checksum of the weights.** Pointing
  `Llama-3.2-1B` at a different GGUF matches and measures something else. Tag
  discipline is yours; a descriptor never sees a path.
- **The KV slot count.** Any two counts disagree slightly on free-text
  logprobs while agreeing on tree shape, so it is an experimental variable —
  but it is measured per box, not declared. It rides on the event instead;
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
