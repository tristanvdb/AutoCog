# AutoCog JSON Schemas

Machine-checkable contracts for all generated artifact formats and configuration files.

## Directory layout

```
share/schemas/
├── common/
│   └── metadata.schema.json     # universal metadata block (format, version, uid, ...)
├── sta.schema.json              # Structured Thoughts Automata (compiled program)
├── ir.schema.json               # Intermediate Representation (pre-state-expansion)
├── fta.schema.json              # Finite Tree Automaton (instantiated prompt)
├── search.schema.json           # search config (share/search/*.json)
├── syntax.schema.json           # syntax config (share/syntax/*.json)
└── README.md                    # this file
```

## Schema dialect

All schemas use **JSON Schema Draft 2020-12**. We use `unevaluatedProperties: false`
(not `additionalProperties: false`) wherever schemas combine via `$ref` or `allOf`,
because `additionalProperties` cannot see through `$ref` — it only considers properties
declared in the same schema object. See the design document for details.

## Metadata block

Every generated artifact (IR, STA, FTA) carries a `metadata` block:

| Field        | Type   | Description |
|------------- |--------|-------------|
| `format`     | enum   | `"ir"`, `"sta"`, or `"fta"` — selects the governing schema |
| `version`    | string | Project version (`x.y.z`) from the VERSION file |
| `uid`        | string | 16-char hex SHA-256 of the JCS-canonicalized artifact (self-excluded) |
| `source_uid` | string | `uid` of the artifact this was derived from |
| `timestamp`  | string | RFC 3339 UTC timestamp |

FTA additionally carries `model_id` in its metadata.

STL source files are human-authored and have no metadata block.
Syntax and search configs are also human-authored and have no metadata.

## Lineage chain

```
STL source (hash of file) ─► IR.source_uid
                              IR.uid ─► STA.source_uid
                                         STA.uid ─► FTA.source_uid
```

## Schema evolution rules

1. **Schema changes require a minor version bump.** If any file under `share/schemas/`
   changes between releases, the minor version in `VERSION` must also change.
   This is enforced by a CI gate at release time.

2. **Strictness is non-negotiable.** `unevaluatedProperties: false` everywhere.
   No escape hatches, no `"we're still designing this"` leniency. If a section is
   in flux, annotate with `"x-status": "unstable"` but keep the validator enforcing.

3. **The golden comparator is the authority.** Both the test comparator and the
   regen script validate against these schemas. Drift between tool output and
   schema is caught immediately.

## Updating goldens

```bash
# Regenerate all golden files and validate against schemas
BUILD_DIR=build tests/update-golden.sh
```

Never run `stlc > golden.json` directly. The regen script validates every
regenerated golden against its schema before writing.
