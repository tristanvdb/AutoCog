# AutoCog JSON Schemas

Machine-checkable contracts for the generated artifact formats and configuration
files. These mirror the `autocog::data` artifact types
(see [docs/compiler/data-codec.md](../../docs/compiler/data-codec.md)).

## Directory layout

```
share/schemas/
├── common/
│   └── metadata.schema.json     # universal metadata block (format, version, hash, timestamp)
├── sta.schema.json              # STA — compiled program
├── fta.schema.json              # FTA — Finite Thought Automaton (instantiated prompt)
├── ftt.schema.json              # FTT — Finite Thought Tree (model evaluation tree)
├── ir.schema.json               # IR — compiler `stlc --ir` debug dump (no metadata block)
├── search.schema.json           # search config (share/search/*.json)
├── syntax.schema.json           # syntax config (share/syntax/*.json)
└── README.md                    # this file
```

`ir.schema.json` validates the compiler's `stlc --ir` output, which is a
debugging dump rather than a stored artifact — it carries no `metadata` block,
which is why `ir` is not one of the metadata `format` values. The `ftt` schema
mirrors `xfta`'s output, where the `metadata` block is optional (the tool does
not finalize the tree, so it is present only once an FTT has been stored).

## Schema dialect

All schemas use **JSON Schema Draft 2020-12**. We use `unevaluatedProperties: false`
(not `additionalProperties: false`) wherever schemas combine via `$ref` or `allOf`,
because `additionalProperties` cannot see through `$ref` — it only considers properties
declared in the same schema object.

## Metadata block

Generated artifacts carry a `metadata` block (`common/metadata.schema.json`), stamped
when the artifact is finalized:

| Field       | Type   | Description |
|-------------|--------|-------------|
| `format`    | enum   | `"sta"`, `"fta"`, `"ftt"`, `"syntax"`, or `"search"` — selects the governing schema |
| `version`   | string | AutoCog project version (`x.y.z`) that produced the artifact |
| `hash`      | string | 64-char lowercase hex — SHA-256 of the artifact's content combined with its provenance (the `metadata` block itself is *not* an input to the hash) |
| `timestamp` | string | RFC 3339 UTC timestamp of finalization |

Artifacts also carry a top-level **`provenance`** map (`role → hash`) recording the
artifacts they were derived from. The hash and provenance together are the artifact's
content-addressed identity; see [data-codec.md](../../docs/compiler/data-codec.md) for
how the hash is computed.

STL source files are human-authored and have no metadata block. The bundled syntax
and search configs (`share/syntax/*.json`, `share/search/*.json`) are likewise authored
without metadata — the block is optional in those schemas and is only stamped when a
config is round-tripped through the artifact store.

## Lineage chain

Provenance forms a derivation chain. The STA is a root artifact (empty provenance, so
its hash is a pure function of content); each derived artifact's provenance carries its
parents' hashes:

```
STA (root)  ─►  FTA.provenance["sta"]
                FTA  ─►  FTT.provenance["fta"]  (+ FTT.provenance["model"] = GGUF hash)
```

## Schema evolution rules

1. **The metadata `version` tracks the AutoCog version.** Schema changes are coupled to
   the version, but the release pipeline reports schema changes since the previous tag
   as *informational* rather than gating on a minor bump (see `release.yaml`). Re-tighten
   that gate if the project readopts strict schema-version coupling.

2. **Strictness is non-negotiable.** `unevaluatedProperties: false` everywhere.
   No escape hatches. If a section is in flux, annotate it with `"x-status": "unstable"`
   but keep the validator enforcing.

3. **The golden comparator is the authority.** Both the test comparator and the regen
   script validate against these schemas, so drift between tool output and schema is
   caught immediately.

## Updating goldens

```bash
# Regenerate all golden files and validate against schemas
BUILD_DIR=build tests/scripts/update-golden.sh
```

Never run `stlc > golden.json` directly. The regen script validates every regenerated
golden against its schema before writing.
