# Multiple Choice Question (MCQ) Demos

Converted from STA format examples in `share/experiments/mcq/`.

## Working Demos

These demos compile successfully to at least parse/scan stage:

### Basic Demos
- **select.stl** - Basic select format (pick index)
- **repeat.stl** - Basic repeat format (verbatim text)

### Chain of Thought (CoT)
- **select-cot.stl** - Select with work[] array for step-by-step reasoning
- **repeat-cot.stl** - Repeat with work[] array for step-by-step reasoning

### Hypothesis-Based (Two-Stage)
- **select-hyp.stl** - Generate hypothesis first, then select from choices
  - Demonstrates: multiple prompts, flow, cross-prompt dataflow (`main.hyphothesis`)
- **repeat-hyp.stl** - Generate hypothesis first, then repeat choice

### Iteration with Reflection
- **select-iter.stl** - Select with retry loop (max 5 attempts)
  - Uses Thought record from stlib
  - Demonstrates: imports, parameterized records, flow with bounds, dataflow channels
  - **NOTE**: Hits "NIY parent scope" at instantiate stage (requires Stage 6)
- **repeat-iter.stl** - Repeat with retry loop (max 5 attempts)
  - Same features as select-iter
  - **NOTE**: Hits "NIY parent scope" at instantiate stage (requires Stage 6)

## Non-Working Demos (Expected Failures)

### Call Channel Issues
- **select-annot.stl** - FAILS: Uses `annotate` as prompt name (reserved keyword)
  - Original demonstrates: call channels with map/bind, per-choice evaluation
  - Error: `Expected token 'identifier' but found 'annotate'`
- **repeat-annot.stl** - FAILS: Same issue as select-annot

## Running the Demos

```bash
# Basic demos (work fully)
stlc share/demos/mcq/select.stl
stlc share/demos/mcq/repeat.stl
stlc share/demos/mcq/select-cot.stl
stlc share/demos/mcq/repeat-cot.stl
stlc share/demos/mcq/select-hyp.stl
stlc share/demos/mcq/repeat-hyp.stl

# Advanced demos (require include path, fail at instantiate stage)
stlc -I share/library share/demos/mcq/select-iter.stl --stage scan
stlc -I share/library share/demos/mcq/repeat-iter.stl --stage scan

# Expected failures
stlc share/demos/mcq/select-annot.stl  # Fails at parse (keyword conflict)
stlc share/demos/mcq/repeat-annot.stl  # Fails at parse (keyword conflict)
```

## select() vs repeat()

- **select(path)**: Returns the **index** of the chosen item (0, 1, 2, 3...)
- **repeat(path)**: Returns the **verbatim text** of the chosen item

Both are "choice" formats that allow the LLM to pick from an array of options.

## Status Summary

- **6 demos fully working** (parse → full compilation)
- **2 demos partially working** (parse → scan, fail at instantiate due to parameterization)
- **2 demos expected to fail** (parse error due to keyword conflict)

Total: 10 MCQ demos converted from STA format
