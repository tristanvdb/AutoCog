# Runtime Semantics & Execution Model

This document describes how a compiled program (an **STA**) is executed. The STA
format and how it is produced are covered in
[Compiler Architecture](./architecture.md); the artifact types used throughout are
covered in [Data & Codec](./data-codec.md).

## Execution pipeline

```
STA  ──instantiate──▶  FTA  ──evaluate──▶  FTT  ──walk──▶  frame
(program)             (one prompt,        (model's        (field
                       text automaton)     eval tree)      values)
```

A program is a graph of prompts. Execution proceeds one prompt at a time. For each
prompt the runtime:

1. **resolves channels** to build the prompt's input `content` (a `Document`);
2. **instantiates** the prompt + content into an **FTA** (a token-level automaton);
3. **evaluates** the FTA against the model to produce an **FTT** (the evaluation tree);
4. **walks** the FTT back into a **frame** (the parsed field values);
5. **branches** to the next prompt — or returns — based on the frame.

Steps 2–4 are a single prompt evaluation; step 1 and 5 are orchestration.

## 1. Prompt evaluation (STA → FTA → FTT → frame)

This is `Engine.evaluate_prompt` (`modules/autocog/engine.py`), which composes the
C++ runtime and backend bindings:

```
fta_id = runtime_sta_cxx.instantiate(sta_id, prompt, content, syntax_id, search_id)
ftt_id = backend_llama_cxx.evaluate(model_id, fta_id)
frame  = runtime_sta_cxx.walk_ftt_to_frame(sta_id, prompt, ftt_id, content)
```

### Instantiate: STA prompt → FTA

`runtime::sta::instantiate` (`libs/autocog/runtime/sta/instantiate.cxx`) unrolls the
prompt's flat field list into a **Finite Thought Automaton** — a DAG of actions,
formatted according to the `Syntax` config, with `SearchConfig` parameters embedded
into each action. Each field becomes one or more actions:

- a **text/completion** field → a `CompleteAction` (free generation, bounded by
  length / threshold / stop);
- an **enum** field → a `ChooseAction` over the fixed values;
- a **select/repeat** field → a `ChooseAction` over the runtime options drawn from
  `content`;
- fixed prompt scaffolding (labels, annotations, separators) → `TextAction`s.

The implicit `next` field (added during compilation) becomes a final `ChooseAction`
over the prompt's flow labels. The FTA's provenance records the STA's hash.

### Evaluate: FTA → FTT

`backend_llama_cxx.evaluate` (`libs/autocog/backend/llama/`) runs the FTA against the
model, exploring it with beam/choice search per the embedded search config, and
produces a **Finite Thought Tree** — a tree of `FTTNode`s carrying the generated
text, token ids, and log-probabilities for each explored path. The FTT's provenance
records the FTA's hash **and** the model's GGUF hash, so an FTT identifies exactly
what produced it.

With the built-in RNG model (`model_id = 0`), generation is deterministic given the
seed (`Engine.set_seed`), which is what the test suite and `--rng` use.

### Walk: FTT → frame

`runtime::sta::walk_ftt_to_frame` (`libs/autocog/runtime/sta/walk.cxx`) selects a
single root→leaf path through the FTT (by default the `"best"` path — highest leaf
score) and maps it back onto the prompt's field schema, producing the **frame**: a
nested `Document` of field name → value. A `select` field's chosen index is resolved
back to its actual value from `content` when available.

## 2. Channels (resolved before instantiation)

Channels populate the prompt's `content` *before* the FTA is built — the model never
sees channel mechanics, only the rendered prompt. Resolution happens in Python
(`modules/autocog/channels.py`, `clauses.py`), in declaration order. There are three
kinds (see the [Language Reference](../structured-thoughts/language.md#channels) for
syntax):

- **get** — read a value from the caller-provided external inputs;
- **use** — read from another prompt's already-computed frame (dataflow), or from a
  previous iteration of the same prompt (self-reference);
- **call** — invoke a Python external or another prompt, optionally transformed by
  clauses (`bind`, `ravel`, `mapped`, `wrap`, `prune`).

Resolving a `call` to another prompt runs that prompt (a nested evaluation) before
the current one is instantiated.

## 3. Flow control (orchestration)

After a prompt is evaluated, `Context.step` (`modules/autocog/context.py`) decides
what happens next using the frame's `next` field:

- if the prompt has **no flows**, it is terminal: the frame becomes the result and
  execution ends;
- the value of `next` selects one of the prompt's flow entries;
- a **return** flow extracts the declared return fields (resolving dotted paths,
  mapping over arrays) and ends execution;
- a **control** flow moves to the named target prompt. If that flow has a limit, the
  per-prompt branch counter is checked first and exceeding it is an error.

The model therefore drives branching by completing the `next` choice, but the set of
legal branches and any loop bounds are fixed at compile time. `Engine.run` repeats
`step` until the context is done or `max_steps` is hit.

## 4. Records and field indexing

Records are **templates copied** (inlined) into each prompt that uses them; the
compiler flattens the resulting nesting into a single field list where each field
carries its `depth` and a per-depth `index` (the index restarts at 0 at each depth
level). Parent pointers run from a leaf field up to the containing prompt, giving a
tree the runtime uses for path resolution. This flattening happens in Stage 6
(Generate) and is what the FTA unrolls and the frame mirrors.

## 5. Determinism and reproducibility

Because every artifact is content-addressed (see [Data & Codec](./data-codec.md)),
the lineage `STA → FTA → FTT` is reproducible: the same STA + content + syntax +
search yields the same FTA hash, and evaluating it with the same model and seed
yields the same FTT hash. This is what lets recorded artifacts (`autocog run
--record`) be compared and replayed.

## Tools

The same pipeline is available as standalone tools for inspection and scripting
(see [C++ Tools](../interfaces/tools-cli.md)):

| Tool   | Stage                | In → Out      |
|--------|----------------------|---------------|
| `stlc` | compile              | STL → STA     |
| `ista` | instantiate          | STA → FTA     |
| `xfta` | evaluate             | FTA → FTT (or best-path text) |
| `psta` | parse / score        | STA + FTT → frame (scores the tree, walks the best path) |
