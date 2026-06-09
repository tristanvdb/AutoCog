# AutoCog Documentation

## Structured Thoughts

- [Overview](./structured-thoughts/overview.md) — Programming model, key concepts, how it works
- [Language Reference](./structured-thoughts/language.md) — STL syntax: records, prompts, channels, flows
- [Standard Library](./structured-thoughts/stdlib.md) — Thought, reflexion, datastore

## Interfaces

- [autocog CLI](./interfaces/autocog-cli.md) — compile, pack, run, serve, rpc, backend
- [C++ Tools](./interfaces/tools-cli.md) — stlc, ista, psta, xfta
- [Python API](./interfaces/python-api.md) — autocog package (compile, Engine, RemoteEngine, etc.)
- [REST API](./interfaces/rest-api.md) — server endpoints (backend, rpc, serve)
- [File Formats](./interfaces/formats.md) — STA, FTA, .stapp, manifest, schema, syntax

## STL Compiler

- [Compiler Architecture](./compiler/architecture.md) — Pipeline stages and data structures
- [Runtime Semantics](./compiler/runtime-semantics.md) — Execution model: STA → FTA → FTT → frame
- [Data & Codec](./compiler/data-codec.md) — Artifact layer, content-addressing, codec, bindings

## Development

- [Developer Guide](../DEVEL.md) — Build setup, testing, containers
- [Test Structure](../tests/README.md) — Test framework, fixtures, coverage

## Examples

- [MCQ Demos](../share/demos/mcq/README.md) — Multiple-choice question examples
- [Story Writer](../share/demos/story-writer/README.md) — Multi-prompt app with loops and externals
