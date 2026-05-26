# File Formats

## STA — Structured Thought Automaton

The compiled output of `stlc` or `autocog compile`. JSON format containing prompts, fields, states, channels, flows, and entry point schemas.

**Extension:** `.sta.json`

**Produced by:** `stlc --emit sta`, `autocog compile`

**Consumed by:** `ista`, `psta`, `autocog run --sta`, `autocog rpc`, `autocog serve`

### Structure

```json
{
  "abi_version": "0.4.4",
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
      "desc": "",
      "fields": [...],
      "states": {...},
      "sequence": [...],
      "flows": {...},
      "channels": [...]
    }
  }
}
```

### Schema Types

Input and output schemas use these types:

| Type | Description | Extra fields |
|------|-------------|-------------|
| `text` | Free-form text | `max_length`, `required` |
| `text` with `enum` | Enumerated value | `enum: [...]`, `required` |
| `array` | Array of items | `items: {type, ...}`, `length` or `min_items`/`max_items` |
| `object` | Nested record | `required` |

Author-provided fields (via schema override): `description`, `default`.

### ABI Version

The `abi_version` field records the autocog version that compiled the STA. At load time, a major version mismatch causes an error; a minor version mismatch causes a warning.

## FTA — Formal Text Automaton

The instantiated automaton ready for model evaluation. Produced by combining an STA prompt with content data and syntax formatting.

**Extension:** `.fta.json`

**Produced by:** `ista`, `runtime_sta_cxx.instantiate()`

**Consumed by:** `xfta`, `backend_llama_cxx.evaluate()`

The FTA contains the text-level automaton with states, transitions, and token constraints. Its structure is internal to the runtime.

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
  "abi_version": "0.4.4",
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
