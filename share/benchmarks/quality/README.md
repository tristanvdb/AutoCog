# benchmarks/quality — accuracy, token overhead, constraint friction

Measures what a *rendering syntax* costs and buys, per model: task
accuracy on ground-truth MCQs, the token overhead the syntax imposes, and
constraint friction — how hard the model fights the structural tokens it
never chose. This is the v0.7.2 scorer and the instrument for the
special-token thesis: a base model's structural friction under
`share/syntax/special.json` is the before-picture a fine-tune must beat,
and the base / instruct / tuned three-way on the same questions is the
v0.7.4 result table.

## How it works

For every (syntax, demo, question):

1. The demo runs through the Python `Engine` (real model or `--rng`),
   with the `Recorder` capturing per-step `input` / `frame` / `fta`
   artifacts. The returned answer is scored against the question's known
   correct choice.
2. Every step's frame is re-encoded with `efta --score` (one batched
   process per run — one model load): the *canonical forced path* under
   that syntax, with real `P(token | prefix)` on **every** token. A
   generated FTT cannot provide this — structural text actions are never
   evaluated during generation; the scored encode prices them.
3. From that single artifact:
   - `tokens.value` / `tokens.structure` — tokens on schema-field nodes
     vs structural nodes (headers, labels, separators): the overhead axis.
   - `friction.value` / `friction.structure` — mean −log P per forced
     token in each class: the friction axis. (These numbers are
     meaningful only since the forced-scoring off-by-one fix; anything
     older measured P(token repeats).)

A (syntax, demo) pair erroring is recorded as a datapoint
(`error.message` on the event), not a crash — base models on structured
syntaxes are expected to be rough.

## Running

Requires the Python package installed (the engine) and a **Release**
build tree for the CLI tools (`stlc`, `efta`) — never benchmark
Debug/coverage builds.

```bash
python3 run.py --build <release-build-dir> --model models/foo.gguf
python3 run.py --build <release-build-dir> --rng \
    --syntaxes default,special --demos select --questions 2   # pipeline smoke
```

Options: `--syntaxes` (default: all five shipped, `special` included),
`--demos` (default: the six answer-scoreable mcq demos), `--questions N`
(limit), `--ctx`, `--out`. `AUTOCOG_NGL=99` offloads to GPU. For cloud
campaigns see `experiments/v0.7/`.

## The question set

`questions.json`: 100 unambiguous general-knowledge MCQs across ~16
topics, each `{id, topic, question, choices[4], answer}` with `answer`
the exact choice text. Correct-answer positions are balanced exactly
25/25/25/25 across the four slots, so a positional prior scores chance —
verified: the tiny smoke model lands at 25% over the full set. The first
ten questions are the original v0.7.2 set (order preserved, choices
reshuffled), so `--questions 10` stays comparable to earlier runs in
spirit but not byte-for-byte. They feed the demos'
`(topic, question, choices)` inputs; correctness compares the returned
answer (or its `answer` field for struct-returning demos) against
`answer`. Add questions freely — keep them unambiguous, keep the answer
verbatim among the choices, and re-balance positions when you add.

## Outputs

`results-<host>-<model>.ndjson` — one ECS-flavored `quality.run` event
per (syntax, demo, question): correctness, wall seconds, step count,
token split, friction split. `results-<host>-<model>.md` — per
(syntax, demo) aggregate: accuracy %, mean tokens, mean frictions. Both
gitignored.

`summarize.py results1.ndjson results2.ndjson ... [--ref MODEL]` builds
the cross-model view from any number of result files: the model x
(syntax, demo) accuracy matrix plus a delta table in percentage points
against a reference model. That delta table is the product for the
base-vs-instruct question — how much a pattern extracts from a base
model relative to its instruct sibling, and how the gap moves with
scale. `experiments/v0.7/run-accuracy.sh` drives the whole campaign
(four Llama-3.2 models x patterns x the full set) as one command.

## Reading the numbers

- **Accuracy** is only meaningful on real models (RNG answers are noise)
  and mainly *between* models/syntaxes on the same cells.
- **Overhead**: `tokens.structure` under a text syntax vs the
  special-token syntax is the compression claim — a marker string's ~10
  tokens vs one reserved id. Under `--rng` (byte-level tokenizer) the
  special syntax looks *more* expensive; that is the RNG artifact, not
  the claim — judge overhead on a real tokenizer.
- **Friction**: high `friction.structure` with decent
  `friction.value` means the model dislikes the scaffolding, not the
  content. Expected extreme for base models under `special` (the tokens
  are semantically inert to them) — that gap closing after fine-tuning,
  at accuracy parity or better and lower total tokens, is the thesis
  datapoint.
