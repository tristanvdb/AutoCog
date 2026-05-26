# Multiple Choice Question (MCQ) Demos

Ten examples demonstrating various thought patterns for answering multiple choice questions.

## Demos

### Basic
- **select.stl** — Pick by index using `select(choices)` format
- **repeat.stl** — Pick by repeating text using `repeat(choices)` format

### Chain of Thought (CoT)
- **select-cot.stl** — Select with `work[]` array for step-by-step reasoning
- **repeat-cot.stl** — Repeat with `work[]` array for step-by-step reasoning

### Hypothesis (Two-Stage)
- **select-hyp.stl** — Generate hypothesis first, then select from choices (multi-prompt, flow, cross-prompt dataflow)
- **repeat-hyp.stl** — Generate hypothesis first, then repeat choice

### Iteration with Reflection
- **select-iter.stl** — Select with retry loop (max 5 attempts), uses `Thought` record from stdlib
- **repeat-iter.stl** — Repeat with retry loop, imports, parameterized records, flow with bounds

### Annotated (Per-Choice Evaluation)
- **select-annot.stl** — Call channels with map/bind for per-choice evaluation
- **repeat-annot.stl** — Same pattern with repeat format

## Running

```bash
# With autocog CLI
autocog compile --stl share/demos/mcq/select.stl
autocog run --stl share/demos/mcq/select.stl --rng \
    --input '{"topic":"Science","question":"What is H2O?","choices":["Water","Fire","Air","Earth"]}'

# With stlc directly
stlc --emit sta share/demos/mcq/select.stl

# Demos using stdlib need the include path (stlc only, autocog adds it automatically)
stlc --emit sta -I share/library/stlib share/demos/mcq/select-iter.stl
```

## select() vs repeat()

- **select(path)** — returns the index of the chosen item (0, 1, 2, 3...)
- **repeat(path)** — returns the verbatim text of the chosen item

Both are "choice" formats that let the model pick from an array of options.
