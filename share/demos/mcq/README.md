# Multiple Choice Question (MCQ) Demos

Two families, named for what the model is choosing among.

**`strings-*`** take a plain list of choice texts.
**`record-*`** take labelled records — `{label, value}`, declared once in
`record.stl`. The label lives in the data, put there when the dataset is
converted; AutoCog has no notion of letter indexing.

Within each family the name says what the model *commits on*:

| program | commits on | resolver |
|---|---|---|
| `strings-select` | the index of a choice | `pick` |
| `strings-repeat` | the choice text | — |
| `record-index` | the index of a record | `pick_value` |
| `record-value` | the record's text | — |
| `record-label` | the record's label | `by_label` |

A frame records what the model emitted: `strings-select` writes `"1"`, not
`"Water"`. Where a commitment is not itself an answer, the program resolves it
with a Python call on its own return (`return call pick _;`), so nothing
between the model and the frame rewrites a field. `mcq.py` holds the three
resolvers.

`record-label` is why this matters. Its predecessor committed on a letter from
a fixed eight-wide vocab regardless of how many choices existed, so a
four-choice question could be answered `"G"`. Here the label is ranked against
the labels the record carries — the choice set *is* the data, and an
out-of-range letter is not something the model can produce.

`record.stl` is a library, not a program: it declares the `choice` record that
the `record-*` demos import, and has no entry point of its own.

## Scaffolds

Each mechanism composes with a reasoning scaffold, as its own program:

- `*-cot` — a `work[]` array for step-by-step reasoning before answering
- `*-hyp` — two prompts: guess first, then choose (flow, cross-prompt dataflow)
- `*-iter` — a bounded retry loop, using the stdlib `Thought` record
- `*-annot` — one sub-prompt per choice (call channels with map/bind),
  annotating each before choosing. These build their own `{value, correct}`
  records from plain string inputs.

## Running

```bash
autocog run --stl share/demos/mcq/strings-select.stl --rng \
    --input '{"topic":"Science","question":"What is H2O?","choices":["Water","Fire","Air","Earth"]}'

autocog run --stl share/demos/mcq/record-label.stl --rng \
    --input '{"topic":"Science","question":"What is H2O?","choices":[{"label":"A","value":"Water"},{"label":"B","value":"Fire"}]}'

# With stlc directly (--sta writes the STA; /dev/stdout for stdout)
stlc --sta /dev/stdout share/demos/mcq/strings-select.stl

# Demos using stdlib need the include path (stlc only; autocog adds it)
stlc --sta /dev/stdout -I share/library/stlib share/demos/mcq/strings-select-iter.stl
```

## select() vs repeat()

- **select(path)** — the model emits the index of the chosen item (0, 1, 2…)
- **repeat(path)** — the model emits the verbatim text of the chosen item

Both constrain the model to the rendered alternatives; they differ in what it
has to produce to name one.
