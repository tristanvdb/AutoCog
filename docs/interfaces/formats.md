# File Formats

## STA — Structured Thought Automaton

The compiled output of `stlc` or `autocog compile`. JSON format containing prompts, fields, states, channels, flows, and entry point schemas.

**Extension:** `.sta.json`

**Produced by:** `stlc --sta`, `autocog compile`

**Consumed by:** `ista`, `psta`, `autocog run --sta`, `autocog rpc`, `autocog serve`

### Structure

```json
{
  "metadata": {
    "format": "sta",
    "version": "<x.y.z>",
    "hash": "<64-hex SHA-256>",
    "timestamp": "2026-06-07T12:00:00Z"
  },
  "provenance": {},
  "entry_points": {
    "main": {
      "prompt": "init_idea",
      "inputs": {
        "query": {"type": "text", "required": true},
        "age": {"type": "text", "enum": ["1",...,"12"], "required": true}
      },
      "outputs": {
        "done": {"type": "text", "required": true}
      }
    }
  },
  "python_imports": {
    "list_templates": {"file": "template.py", "target": "list_templates"}
  },
  "prompts": {
    "init_idea": {
      "name": "init_idea",
      "desc": [...],
      "fields": [...],
      "abstracts": [...],
      "flows": {...},
      "channels": [...],
      "search": {...},
      "vocabs": {...}
    }
  }
}
```

The exact shape is fixed by `share/schemas/sta.schema.json`. `abstracts` is the
abstract state graph the runtime unrolls into an FTA; `vocabs` is the prompt's
vocab table.

### Schema Types

Input and output schemas use these types:

| Type | Description | Extra fields |
|------|-------------|-------------|
| `text` | Free-form text | `max_length`, `required` |
| `text` with `enum` | Enumerated value | `enum: [...]`, `required` |
| `array` | Array of items | `items: {type, ...}`, `length` or `min_items`/`max_items` |
| `object` | Nested record | `required` |

Author-provided fields (via schema override): `description`, `default`.

### Metadata & versioning

Every STA carries a `metadata` block (`format`, `version`, `hash`, `timestamp`)
and a `provenance` map. `version` records the autocog version that compiled the
STA, and `hash` is its content-addressed identity. The STA is a root artifact, so
its `provenance` is empty and its `hash` is a pure function of its content. See the
[data layer](../compiler/data-codec.md) for the artifact model.

## FTA — Finite Thought Automaton

The instantiated automaton ready for model evaluation. Produced by combining an STA
prompt with resolved channel content and syntax/search configuration.

**Extension:** `.fta.json`

**Produced by:** `ista`, `runtime_sta_cxx.instantiate()`

**Consumed by:** `xfta`, `backend_llama_cxx.evaluate()`

The FTA is a DAG of actions (`text`, `complete`, `choose`) with token constraints and
embedded search parameters; it carries the source STA's hash in its provenance. Its
structure is fixed by `share/schemas/fta.schema.json`. Evaluating an FTA produces an
**FTT** (Finite Thought Tree), the model's evaluation tree, which the runtime walks
back into a frame of field values (see [Runtime Semantics](../compiler/runtime-semantics.md)).

## .stapp — Structured Thought App

A zip archive containing everything needed to run an STL program.

**Extension:** `.stapp`

**Produced by:** `autocog pack`

**Consumed by:** `autocog run --app`, `autocog serve --app`, `autocog rpc --app`

### Contents

```
app.stapp (zip)
├── manifest.json           Metadata and schemas
├── program.sta.json        Pre-compiled STA (optional)
├── source.stl              STL source (optional)
├── externals/              Python external files
│   ├── template.py
│   └── book.py
├── stl/                    Imported STL files (non-stdlib)
│   ├── book.stl
│   └── template.stl
└── stlib/                  Vendored stdlib (only with --vendor-stdlib)
    ├── thoughts.stl
    └── datastore.py
```

At least one of `program.sta.json` or the source STL must be present.

### manifest.json

```json
{
  "name": "story-writer",
  "version": "1.0",
  "abi_version": "<x.y.z>",
  "entry_points": {
    "main": {
      "prompt": "init_idea",
      "inputs": {
        "query": {"type": "text", "required": true, "description": "Story topic"},
        "age": {"type": "text", "enum": [...], "required": true, "description": "Target age"}
      },
      "outputs": {
        "done": {"type": "text", "required": true, "description": "The story"}
      }
    }
  },
  "python_imports": {
    "list_templates": {"file": "template.py", "target": "list_templates"}
  },
  "program": "program.sta.json",
  "source": "writer.stl",
  "imports": ["book.stl", "template.stl"],
  "externals": ["book.py", "template.py"],
  "vendor_stdlib": false
}
```

The `entry_points` section mirrors the STA format, optionally enriched with author-provided descriptions and defaults from a schema override file.

## schema.json — Schema Override

Optional file placed alongside the STL source. Merged into the manifest during `autocog pack` to add descriptions, defaults, and other annotations to the auto-generated schema.

**Auto-detected from:** same directory as the `--stl` source file.

**Explicit:** `autocog pack --schema schema.json`

### Structure

```json
{
  "main": {
    "inputs": {
      "query": {"description": "What kind of story to write"},
      "age": {"description": "Target reader age (1-12)", "default": "5"}
    },
    "outputs": {
      "done": {"description": "The formatted story as markdown"}
    }
  }
}
```

Only include fields you want to override. The auto-generated type, required, and enum fields are preserved.

## Syntax JSON

Formatting rules for how prompts are rendered as text. Controls field labels, indentation, separators, and prompt structure.

**Default:** auto-detected from the installed stdlib path.

**Custom:** `--syntax path/to/syntax.json`

**Used by:** `ista`, `psta`, `autocog run`, `autocog serve`, `autocog rpc`

The syntax file format is documented in `share/syntax/default.json`.

## Search Config (`.json`)

Tunes how the runtime explores generations during FTA evaluation. Parameters are
grouped by category (matching the `search { … }` policies in STL):

```json
{
  "text":   { "threshold": 0.1, "beams": 4, "ahead": 2, "width": 1, "repetition": null, "diversity": null },
  "enum":   { "threshold": 0.1, "width": 1 },
  "branch": { "threshold": 0.1, "width": 1 },
  "flow":   { "threshold": 0.1, "width": 1 },
  "queue":  { "metric": "perplexity" }
}
```

- `text` — completion sampling: `threshold` (prune below this probability),
  `beams`, `ahead` (look-ahead tokens), `width` (beams kept), plus optional
  `repetition` / `diversity` penalty weights (`null` = disabled). The schema
  requires `threshold`, `beams`, `ahead`, `width`.
- `enum` / `branch` / `flow` — choice-style decisions: `threshold` and `width`.
- `queue` — global work-queue ordering: `metric` (e.g. `"perplexity"`).

The shape is fixed by `share/schemas/search.schema.json`. These correspond to the
`search { text.* / enum.* / branch.* / flow.* / queue.* }` policies in STL (see
[Search Policies](../structured-thoughts/language.md#search-policies)).

Bundled configurations:
- `share/search/default.json` — standard settings
- `share/search/fast.json` — minimal search (fewer beams / look-ahead)
- `share/search/full.json` — broader search
