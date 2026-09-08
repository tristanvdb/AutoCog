# Thinking Library

Reusable STL thinking patterns: records that give thoughts structure, and
prompts that package a reasoning strategy behind a callable interface. All of
it is plain STL — nothing here is privileged; the library is expected to grow
and change (it is a deliberately-evolving surface, per the roadmap).

## Catalog

| File | Exports | Pattern |
|------|---------|---------|
| `thought.stl` | `Thought` | The core record: one parameterized unit of reasoning |
| `reflexion.stl` | `reflexion` | Refine an initial attempt through private working steps |
| `brainstorm.stl` | `brainstorm` | Diverge: many rough ideas, quantity over polish |
| `critique.stl` | `Critique`, `critique` | Review a draft into structured (issue, severity, suggestion) entries |
| `revise.stl` | `revise` | Converge: apply feedback to a draft (pairs with `critique`) |
| `decompose.stl` | `Step`, `decompose` | Plan-then-act: ordered steps, each with a success check |
| `hypothesize.stl` | `Hypothesis`, `hypothesize` | Guess-then-check: candidates with explicit confidence |
| `deliberate.stl` | `Perspective`, `deliberate` | Debate: argue distinct stances, then synthesize |
| `thoughts.stl` | everything above | Umbrella; one import for the whole library |

Import individual patterns or the umbrella; the stdlib root is always on the
include path:

```
from "thinking/decompose.stl" import Step, decompose;   // one pattern
from "thinking/thoughts.stl" import Thought, reflexion; // via the umbrella
```

## Conventions

Every pattern follows the same callable-prompt shape, so callers can swap one
strategy for another without restructuring:

- **Inputs are `get` channels** (`task`, `draft`, `question`, ...) — the caller
  maps its own fields onto them in the call body.
- **Arguments tune, inputs feed.** Compile-time `argument`s (lengths, counts,
  `subject`) shape the prompt; runtime data flows through channels.
- **Returns are labeled.** The polished output is `response` (or a
  pattern-specific name like `steps`, `critiques`, `hypotheses`); working
  thoughts are returned as `intermediate` so orchestrating programs can record
  or inspect them. Pick the field with `bind`:

```
channel {
  plan call decompose<subject="the release", max_steps=6> {
    task get task;
  } bind(_,steps);
}
```

- **Structured results are records** (`Critique`, `Step`, `Hypothesis`,
  `Perspective`) — importable by callers, so downstream prompts can consume
  fields (`feedback.issue`, `steps.action`) instead of re-parsing prose.

## Composing patterns

Patterns are designed to chain. `critique` → `revise` is `reflexion`
decomposed into two stages, which moves the improvement loop out of the prompt
and into the program — where flows can bound it and channels can observe it:

```
prompt review {
  is { feedback[1:5] is Critique; }
  channel {
    feedback call critique<subject="the draft"> { draft get draft; } bind(_,critiques);
  }
  flow rework;
}

prompt rework {
  is { improved[1:20] is text<length=30>; }
  channel {
    improved call revise<subject="the draft"> {
      draft get draft;
      feedback use review.feedback.issue;
    } bind(_,response);
  }
  return { use improved; }
}
```

Other natural chains: `brainstorm` → `critique` → `revise` (diverge, judge,
converge), `decompose` → per-step prompts (plan then act), `hypothesize` →
verification prompts (guess then check).

## Testing

Each pattern has a driver program under `tests/integration/stdlib/thinking/`
that imports it and exercises the call/bind path. Per driver, ctest runs a
compile smoke (`stlc --ir`) and the full pipeline
(`stlc → ista → xfta --rng → psta`). The umbrella driver imports every export,
so a pattern added here without a matching `thoughts.stl` line (or vice versa)
fails the suite.

## Adding a pattern

1. One file per pattern; import `Thought` from `thinking/thought.stl` rather
   than redefining it.
2. Follow the conventions above (get-inputs, labeled returns, records for
   structured results, `subject` + tuning arguments with defaults).
3. Re-export the new names from `thoughts.stl`.
4. Add a driver fixture in `tests/integration/stdlib/thinking/fixtures/`
   (auto-discovered by the CMake glob) and extend the umbrella driver's
   imports.
5. Document the pattern here and in `docs/structured-thoughts/stdlib.md`.
