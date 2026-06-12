# STL Compiler Architecture

## Overview

The STL (Structured Thoughts Language) compiler transforms source files into an
**STA** (Structured Thought Automaton) — a content-addressed artifact the runtime
instantiates and a language model executes. The compiler is C++; it is exposed to
the `autocog` Python package and to the `stlc` command-line tool, both of which
drive the same `Driver`.

- **Implementation:** `libs/autocog/compiler/stl/`
- **Entry points:** `tools/stlc/` (CLI), `bindings/compiler-stl/` (Python)
- **Output artifact:** `autocog::data::STA`, serialized by `autocog::codec`

## Compilation Pipeline

The `Driver` runs six sequential stages (`driver.hxx`, `CompilationStage`). Each
stage checks for errors and stops early if any were reported; `driver.stage` also
lets a caller stop after any stage to inspect its output (this is what `stlc`'s
per-format emit flags do — see below).

```
STL source files
    │
 ┌──┴── Stage 1: Parse        → AST              (driver.programs)
 │      Stage 2: Symbols      → SymbolTable      (driver.tables)
 │      Stage 3: Globals      → Evaluator        (driver.evaluator)
 │      Stage 4: Instantiate  → InstantiationGraph (driver.graph)
 │      Stage 5: Assemble     → IR               (driver.records, driver.prompts)
 └──┬── Stage 6: Generate     → data::STA        (driver.sta)
    │
    ▼
 codec::to_json(driver.sta)  → STA JSON
```

### Stage 1 — Parse

Parses each input file into an Abstract Syntax Tree.

- **Method:** `run_parse()` — `compile/parse.cxx`
- **Output:** `driver.programs` (one `ast::Program` per file) and a `fileids` map
- **Implementation:** RE-flex lexer (`lexer.l`) + recursive-descent parser
  (`parser.cxx`, `parser/*.cxx`), building nodes from `ast.hxx` / `ast/*`
- **Node types:** 46, enumerated in `ast/nodes.def` (the single `X(...)` list that
  drives the node templates, the printer, and the serializer)

### Stage 2 — Symbols

Collects every declared symbol (records, prompts, defines, imports, aliases,
Python externals) into a symbol table, and resolves imports and aliases to the
symbols they name.

- **Method:** `run_symbols()` — `compile/symbols.cxx`
- **Output:** `driver.tables` (a `SymbolTable`: qualified-name → symbol, plus a
  per-file context map)
- **Implementation:** `symbol-scanner.{hxx,cxx}`, `symbol-table.hxx`,
  `symbols.{hxx,cxx}`
- **Symbol kinds:** `DefineSymbol`, `RecordSymbol`, `PromptSymbol`, `PythonSymbol`,
  and the transient `UnresolvedImport` / `UnresolvedAlias`

### Stage 3 — Globals

Evaluates file-level `define` statements and command-line `-D` defines to constant
values, producing the `Evaluator` that later stages use to resolve expressions.

- **Method:** `run_globals()` — `compile/globals.cxx`
- **Output:** `driver.evaluator`
- **Implementation:** `evaluate.{hxx,cxx}` (constant-folding over `ast::Expr`)

### Stage 4 — Instantiate

Builds the instantiation graph: one node per distinct `(record-or-prompt,
arguments)` combination, reached by BFS from the exported entry points, with name
mangling, cycle detection, and argument/context propagation.

- **Method:** `run_instantiate()` — `compile/instantiate.cxx`
- **Output:** `driver.graph` (an `InstantiationGraph` of nodes keyed by mangled
  name, with directed reference edges)
- **Implementation:** `instantiation-graph.{hxx,cxx}`

### Stage 5 — Assemble

Lowers the instantiation graph to the compiler's Intermediate Representation:
concrete records and prompts with their fields, formats, channels, flows, vocabs,
and resolved search policies. Records are inlined (copied) into the prompts that
reference them.

- **Method:** `run_assemble()` — `compile/assemble.cxx`
- **Output:** `driver.records`, `driver.prompts` (IR, `ir.hxx`)
- **Key IR types:** `ir::Prompt`, `ir::Record`, `ir::Field`, and the leaf formats
  `Completion`, `Enum`, `Choice`

### Stage 6 — Generate

Converts the IR into the `autocog::data::STA` artifact: it flattens each prompt's
nested fields into a depth/index-indexed list, appends the implicit `next`
flow-selection field, builds the abstract state graph, extracts flows and channels,
computes per-entry-point input/output JSON schemas, and finalizes the artifact
(stamping its content hash).

- **Method:** `run_generate()` — `compile/generate.cxx`
- **Output:** `driver.sta` (`autocog::data::STA`)
- **Boundary:** this is where the compiler crosses from compiler-internal IR into
  the shared [data layer](./data-codec.md); the STA is the first artifact in the
  STA → FTA → FTT lineage

## Emit Outputs

`stlc` has one output flag per format, each taking a destination file
(`/dev/stdout` for stdout). At least one is required, and several may be combined
in a single run — the pipeline runs up to the deepest requested stage and each
output is serialized from its stage (see `serialize/*.cxx`).

| Flag    | Stage       | Serializer            | Output |
|---------|-------------|-----------------------|--------|
| `--ast`   | Parse       | `serialize/ast.cxx`   | Parsed AST |
| `--graph` | Instantiate | `serialize/graph.cxx` | Instantiation graph |
| `--ir`    | Assemble    | `serialize/ir.cxx`    | Intermediate representation |
| `--sta`   | Generate    | `serialize/sta.cxx`   | **STA** (via `codec::to_json`) |

Only `--sta` is a content-addressed artifact validated against a published schema;
the others are debugging/inspection dumps. (The intermediate Symbols and Globals
stages still run but are no longer separately emittable.)

## Key Data Structures

| Structure | Location | Purpose |
|-----------|----------|---------|
| **AST** | `ast.hxx`, `ast/*`, `ast/nodes.def` | Parse-tree of the source (46 node types) |
| **SymbolTable** | `symbol-table.hxx`, `symbols.hxx` | Qualified name → symbol; per-file contexts |
| **InstantiationGraph** | `instantiation-graph.hxx` | All `(type, args)` instantiations, mangled |
| **IR** | `ir.hxx` | Concrete prompts/records/fields/channels/flows |
| **data::STA** | `data/sta.hxx` | Final compiled artifact (see [data-codec.md](./data-codec.md)) |

## Output Format

`codec::to_json(driver.sta)` produces the STA JSON. Its shape is fixed by
`share/schemas/sta.schema.json`:

```json
{
  "metadata": {
    "format": "sta",
    "version": "<x.y.z>",
    "hash": "<64-hex SHA-256 of content + provenance>",
    "timestamp": "2026-06-07T12:00:00Z"
  },
  "provenance": {},
  "entry_points": {
    "main": { "prompt": "<mangled prompt name>", "inputs": {...}, "outputs": {...} }
  },
  "python_imports": { "<extern>": { "file": "...", "target": "..." } },
  "prompts": {
    "<mangled name>": { "fields": [...], "abstracts": [...], "flows": {...}, "channels": [...] }
  }
}
```

The STA is a *root* artifact: its provenance is empty, so its hash is a pure
function of its content. See [File Formats](../interfaces/formats.md) and the
[data layer](./data-codec.md) for the artifact model, and
[Runtime Semantics](./runtime-semantics.md) for how the STA is executed.

## Build System

- **Build tool:** CMake (C++17)
- **Dependencies:** RE-flex (lexer/parser runtime), nlohmann/json (codec JSON),
  pybind11 (Python bindings), picosha2 (artifact content hashing)

## Testing

- **Parser units:** `tests/units/libs/autocog/compiler/stl/parser/` (JSON-driven
  grammar-production fixtures)
- **Stage golden output:** `tests/integration/tools/stlc/{ir,sta,emit}/` diff each
  emit target against checked-in goldens (see [tests/README.md](../../tests/README.md))
- **Fixtures:** `tests/fixtures/stl/`
