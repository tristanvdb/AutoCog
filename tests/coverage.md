# Coverage Report

Generated: 2026-05-24 23:07:55

## Summary

| Category | Lines | Covered | Coverage |
|----------|------:|--------:|---------:|
| C++      | 5916 | 4942 | 84% |
| C++ (templates) | 1614 | 851 | 53% |
| Python   | 371 | 321 | 87% |
| **Total** | **7901** | **6114** | **77%** |
| **Total (excl. templates)** | **6287** | **5263** | **84%** |

## C++ Coverage by Component

| Component | Lines | Covered | Coverage |
|-----------|------:|--------:|---------:|
| `bindings/backend-llama` | 35 | 20 | 57% |
| `bindings/compiler-stl` | 17 | 12 | 71% |
| `bindings/runtime-sta` | 60 | 40 | 67% |
| `libs/autocog/backend/llama` | 663 | 563 | 85% |
| `libs/autocog/compiler/stl` | 3705 | 3236 | 87% |
| `libs/autocog/runtime/fta` | 66 | 60 | 91% |
| `libs/autocog/runtime/sta` | 610 | 510 | 84% |
| `libs/autocog/runtime/store` | 51 | 51 | 100% |
| `libs/autocog/utilities/exception.cxx` | 184 | 88 | 48% |
| `libs/autocog/utilities/exception.hxx` | 58 | 13 | 22% |
| `tools/ista` | 52 | 50 | 96% |
| `tools/psta` | 54 | 52 | 96% |
| `tools/stlc` | 245 | 165 | 67% |
| `tools/xfta` | 116 | 82 | 71% |
| **Total** | **5916** | **4942** | **84%** |

### C++ Per-File Detail

<details><summary><code>libs/autocog/backend/llama</code></summary>

| File | Lines | Covered | Coverage |
|------|------:|--------:|---------:|
| `convert.cxx` | 130 | 122 | 94% |
| `evaluation-choice.cxx` | 36 | 34 | 94% |
| `evaluation-completion.cxx` | 125 | 76 | 61% |
| `evaluation-text.cxx` | 14 | 11 | 79% |
| `evaluation.cxx` | 64 | 62 | 97% |
| `manager.cxx` | 58 | 53 | 91% |
| `manager.hxx` | 1 | 1 | 100% |
| `model.cxx` | 235 | 204 | 87% |

</details>

<details><summary><code>libs/autocog/compiler/stl</code></summary>

| File | Lines | Covered | Coverage |
|------|------:|--------:|---------:|
| `ast.cxx` | 16 | 8 | 50% |
| `ast/annot.hxx` | 4 | 2 | 50% |
| `ast/channel.hxx` | 18 | 9 | 50% |
| `ast/define.hxx` | 2 | 1 | 50% |
| `ast/expr.cxx` | 19 | 0 | 0% |
| `ast/expr.hxx` | 20 | 10 | 50% |
| `ast/flow.hxx` | 4 | 2 | 50% |
| `ast/path.hxx` | 10 | 5 | 50% |
| `ast/printer.hxx` | 490 | 490 | 100% |
| `ast/program.hxx` | 6 | 4 | 67% |
| `ast/prompt.hxx` | 2 | 2 | 100% |
| `ast/record.hxx` | 2 | 2 | 100% |
| `ast/return.hxx` | 4 | 2 | 50% |
| `ast/search.hxx` | 4 | 2 | 50% |
| `ast/struct.hxx` | 12 | 6 | 50% |
| `compile/assemble.cxx` | 249 | 215 | 86% |
| `compile/generate.cxx` | 381 | 371 | 97% |
| `compile/globals.cxx` | 50 | 37 | 74% |
| `compile/instantiate.cxx` | 38 | 32 | 84% |
| `compile/parse.cxx` | 10 | 10 | 100% |
| `compile/symbols.cxx` | 8 | 8 | 100% |
| `diagnostic.cxx` | 53 | 36 | 68% |
| `driver.cxx` | 83 | 61 | 73% |
| `driver.hxx` | 6 | 4 | 67% |
| `eval-utils.txx` | 40 | 38 | 95% |
| `evaluate.cxx` | 191 | 129 | 68% |
| `instantiation-graph.cxx` | 193 | 170 | 88% |
| `instantiation-graph.hxx` | 8 | 8 | 100% |
| `ir.hxx` | 150 | 106 | 71% |
| `lexer.l` | 109 | 104 | 95% |
| `parser-state.cxx` | 32 | 32 | 100% |
| `parser.cxx` | 92 | 86 | 93% |
| `parser/alias.cxx` | 6 | 6 | 100% |
| `parser/annotate.cxx` | 21 | 21 | 100% |
| `parser/assign.cxx` | 5 | 5 | 100% |
| `parser/call.cxx` | 8 | 8 | 100% |
| `parser/channel.cxx` | 8 | 8 | 100% |
| `parser/clauses.cxx` | 39 | 39 | 100% |
| `parser/define.cxx` | 12 | 11 | 92% |
| `parser/expression.cxx` | 129 | 105 | 81% |
| `parser/field.cxx` | 16 | 16 | 100% |
| `parser/fieldref.cxx` | 7 | 7 | 100% |
| `parser/flow.cxx` | 24 | 24 | 100% |
| `parser/format.cxx` | 54 | 52 | 96% |
| `parser/import.cxx` | 20 | 18 | 90% |
| `parser/kwarg.cxx` | 41 | 27 | 66% |
| `parser/link.cxx` | 36 | 36 | 100% |
| `parser/objectref.cxx` | 9 | 9 | 100% |
| `parser/path.cxx` | 18 | 18 | 100% |
| `parser/program.cxx` | 48 | 38 | 79% |
| `parser/prompt.cxx` | 55 | 48 | 87% |
| `parser/record.cxx` | 43 | 36 | 84% |
| `parser/return.cxx` | 52 | 52 | 100% |
| `parser/search.cxx` | 17 | 17 | 100% |
| `parser/struct.cxx` | 7 | 7 | 100% |
| `serialize/ast.cxx` | 11 | 11 | 100% |
| `serialize/globals.cxx` | 7 | 6 | 86% |
| `serialize/graph.cxx` | 30 | 30 | 100% |
| `serialize/helpers.hxx` | 17 | 14 | 82% |
| `serialize/ir.cxx` | 220 | 196 | 89% |
| `serialize/sta.cxx` | 151 | 147 | 97% |
| `serialize/symbols.cxx` | 33 | 22 | 67% |
| `symbol-scanner.cxx` | 125 | 99 | 79% |
| `symbol-table.cxx` | 45 | 42 | 93% |
| `symbols.cxx` | 6 | 6 | 100% |
| `symbols.txx` | 24 | 24 | 100% |
| `token.cxx` | 55 | 39 | 71% |

</details>

<details><summary><code>libs/autocog/runtime/fta</code></summary>

| File | Lines | Covered | Coverage |
|------|------:|--------:|---------:|
| `fta.cxx` | 31 | 30 | 97% |
| `fta.hxx` | 12 | 9 | 75% |
| `ftt.cxx` | 23 | 21 | 91% |

</details>

<details><summary><code>libs/autocog/runtime/sta</code></summary>

| File | Lines | Covered | Coverage |
|------|------:|--------:|---------:|
| `instantiate.cxx` | 172 | 158 | 92% |
| `load.cxx` | 295 | 241 | 82% |
| `parse.cxx` | 88 | 74 | 84% |
| `sta.cxx` | 39 | 22 | 56% |
| `state.hxx` | 4 | 4 | 100% |
| `value.hxx` | 12 | 11 | 92% |

</details>

<details><summary><code>libs/autocog/runtime/store</code></summary>

| File | Lines | Covered | Coverage |
|------|------:|--------:|---------:|
| `store.cxx` | 9 | 9 | 100% |
| `store.hxx` | 42 | 42 | 100% |

</details>

<details><summary><code>tools/stlc</code></summary>

| File | Lines | Covered | Coverage |
|------|------:|--------:|---------:|
| `args.cxx` | 222 | 144 | 65% |
| `main.cxx` | 23 | 21 | 91% |

</details>

## C++ Templates (inflated by instantiation)

These files have gcovr line counts inflated by template instantiations.
Each `if constexpr` branch is counted per instantiation, most are structurally dead.

| File | Lines (gcovr) | Covered | Coverage |
|------|------:|--------:|---------:|
| `ast.hxx` | 830 | 530 | 64% |
| `nodes.def` | 44 | 18 | 41% |
| `parser.hxx` | 415 | 218 | 53% |
| `symbol-scanner.hxx` | 325 | 85 | 26% |
| **Total** | **1614** | **851** | **53%** |

## Python Coverage

| Module | Lines | Covered | Coverage |
|--------|------:|--------:|---------:|
| `autocog/__init__.py` | 3 | 3 | 100% |
| `autocog/channels.py` | 118 | 98 | 83% |
| `autocog/clauses.py` | 113 | 106 | 94% |
| `autocog/context.py` | 81 | 68 | 84% |
| `autocog/engine.py` | 27 | 19 | 70% |
| `autocog/program.py` | 29 | 27 | 93% |
| **Total** | **371** | **321** | **87%** |

