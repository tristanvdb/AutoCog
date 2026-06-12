# Data & Codec Layers

AutoCog's compiled and runtime artifacts (STA, FTA, FTT, plus the syntax and
search configurations) all live in a single C++ layer, `autocog::data`, and are
converted to/from JSON and Python by a sibling layer, `autocog::codec`. Everything
the compiler emits and the runtime consumes flows through these two layers.

- **Data:** `libs/autocog/data/` — the artifact types and a content-addressed store
- **Codec:** `libs/autocog/codec/` — JSON ↔ artifact and Python ↔ artifact conversion
- **Bindings:** `bindings/` — expose the store, the compiler, the runtime, and the
  backend to Python as content-handle APIs

## Why two layers

The `data` types carry **no** external dependencies — no nlohmann/json, no
pybind11. They are plain structs with identity and provenance. All conversion to
outside formats lives in `codec` as free-function specializations, pulled in only
at I/O boundaries (the tools and the bindings). This keeps the artifact model
small and lets the same type serialize to a file, a JSON string, or a Python
object without the type itself knowing about any of those.

## The artifact model

Every stored artifact derives from `Base<CRT>` (`data/base.hxx`) and gains:

- a **`metadata`** header (`data/metadata.hxx`): `{ format, version, hash, timestamp }`
- a **`provenance`** map: `role → hash` of the ancestor artifacts it was derived from
- a **`finalize()`** call that computes and stamps the content hash

### Artifact types

| Type | `format` | Purpose | Provenance |
|------|----------|---------|-----------|
| `STA` (`data/sta.hxx`) | `"sta"` | Compiled program: entry points, Python imports, prompts | root (empty) |
| `FTA` (`data/fta.hxx`) | `"fta"` | Finite Thought Automaton: a DAG of actions for one instantiated prompt | inherits the STA |
| `FTT` (`data/ftt.hxx`) | `"ftt"` | Finite Thought Tree: the backend's evaluation tree over an FTA | inherits FTA + model |
| `Syntax` (`data/syntax.hxx`) | `"syntax"` | Prompt-formatting configuration | root |
| `SearchConfig` (`data/search.hxx`) | `"search"` | Beam/choice/queue search parameters | root |

A sixth type, `Document` (`utilities/types.hxx`), is the dynamic nested value used
for channel inputs, frames, and outputs. It is *transient* — never hashed or
stored — and has codec converters but no `Base`.

### Content addressing

`finalize()` computes `hash = SHA-256( SHA-256(provenance) ‖ content_hash() )`
(`data/base.txx`, hashing via picosha2 in `data/utility.*`):

- `content_hash()` folds the artifact's fields in declaration order (field *names*
  are excluded; floats hash by their exact IEEE-754 bits) into a SHA-256 digest.
- the provenance map is hashed as `role=hash;…` and folded in.

The hash *is* the artifact's identity. Two artifacts with identical content and
provenance produce the same 64-hex hash. Because a derived artifact's provenance
records its parents' hashes, an FTT's hash transitively pins the exact STA, FTA,
and model (by GGUF SHA-256) that produced it — re-running the same inputs yields
the same hash.

### The datastore

`DataStore` (`data/store.hxx`) is a process-wide singleton — `datastore()` — with
one `Registry<T>` (`data/registry.hxx`) per artifact type: `syntax`, `search`,
`sta`, `fta`, `ftt`. A registry is a content-addressed, reference-counted, mutex-
guarded pool:

- `add(artifact)` finalizes it, then either inserts a new entry or, if the hash
  already exists, bumps the holder count — identical content is deduplicated.
- `get(id)` returns the artifact by hash; `release(id)` drops a holder and frees
  the entry when the count reaches zero.

Handles passed across the Python boundary are just these hashes.

## Codec

`codec` declares two conversion families and specializes each per type in
`<type>-json.cxx` / `<type>-python.cxx`:

```cpp
// JSON (nlohmann) — codec/json.hxx
template <class T> nlohmann::json   to_json(T const & obj);
template <class T> void             from_json(nlohmann::json const & dom, T & out);
template <class T> std::unique_ptr<T> from_json(nlohmann::json const &);   // construct+fill+finalize
template <class T> std::string      to_string(T const &);   // + from_string
template <class T> void             to_file(T const &, std::string const & path);  // + from_file

// Python (pybind11) — codec/python.hxx
template <class T> pybind11::object to_py(T const & obj);
template <class T> void             from_py(pybind11::object const &, T & out);
template <class T> std::unique_ptr<T> from_py(pybind11::object const &);   // construct+fill+finalize
```

The owning `from_json` / `from_py` overloads `finalize()` the result, so a value
read back in re-verifies its hash. The file/string paths go through JSON; the
Python paths convert directly to/from `dict`/`list`/scalars with **no** JSON
intermediation — the bindings cross the boundary without serializing to text.

Variant-bodied structures (e.g. an FTA `Action` that is `TextAction` /
`CompleteAction` / `ChooseAction`, or a `Channel` that is input / dataflow / call)
are discriminated by a `"type"` field and dispatched with an `overloaded` visitor.

## Bindings

Three pybind11 modules sit on top of `data` + `codec`. They never expose artifact
objects directly — every call takes or returns a string **handle** (content hash)
into `datastore()`.

### `runtime_sta_cxx` (`bindings/runtime-sta`)

For each of `syntax`, `search`, `sta`, `fta`, `ftt`, a uniform six-verb store API
(generated by the `AUTOCOG_BIND_STORE` macro):

| Verb | Direction |
|------|-----------|
| `load_<x>(path)`  | JSON file → datastore (returns handle) |
| `read_<x>(obj)`   | Python object → datastore (returns handle) |
| `get_<x>(id)`     | datastore → Python object |
| `store_<x>(id, path)` | datastore → JSON file |
| `dump_<x>(id)`    | datastore → JSON string |
| `release_<x>(id)` | drop a holder |

Plus the runtime verbs: `instantiate(sta_id, prompt, content, syntax_id, search_id)
→ fta_id` and `walk_ftt_to_frame(sta_id, prompt, ftt_id, content) → frame`.

### `compiler_stl_cxx` (`bindings/compiler-stl`)

`compile(...) → sta_id` (drives the [Driver pipeline](./architecture.md) and stores
the resulting STA), `get_diagnostics(...)`, `release(...)`.

### `backend_llama_cxx` (`bindings/backend-llama`)

`create(model_path, n_ctx) → model_id`, `set_seed`, `tokenize` / `detokenize`,
`vocab_size`, `build_info`, and `evaluate(model_id, fta_id) → ftt_id` (runs the FTA
through the model and stores the resulting FTT, stamped with FTA + model
provenance).

The Python `Engine` composes these: `instantiate` → `evaluate` → `walk_ftt_to_frame`
(see [Runtime Semantics](./runtime-semantics.md)).

## Schemas

The JSON form of each artifact is a published, machine-checkable contract under
`share/schemas/` (`sta`, `fta`, `ftt`, `ir`, `search`, `syntax`, all sharing
`common/metadata.schema.json`). See
[share/schemas/README.md](../../share/schemas/README.md).
