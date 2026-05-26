# Tests

## Structure

```
tests/
├── fixtures/                    Shared test data (read-only, no test logic)
│   ├── stl/                     STL source files
│   │   ├── language/            Pure STL language features (no runtime needed)
│   │   │   ├── structures/      Records, prompts, fields, nesting, arrays
│   │   │   ├── formats/         Text, enum, select, repeat
│   │   │   ├── expressions/     Defines, parameterized records/prompts
│   │   │   ├── symbols/         Import, export, alias
│   │   │   ├── annotations/     Prompt/field annotations
│   │   │   └── search/          Search constructs
│   │   ├── flows/               Execution semantics (need runtime to test)
│   │   │   ├── control/         Flow statements (simple, loop with limit)
│   │   │   ├── return/          Return statements (simple, labeled)
│   │   │   ├── data/            Channels: input (get), dataflow (use), self-ref
│   │   │   ├── calls/           Call channels (basic, mapped, bind)
│   │   │   └── transform/       Clause modifiers (ravel, wrap, prune)
│   │   └── programs/            Composition of multiple features
│   └── fta/                     FTA JSON fixtures (pre-tokenized)
│
├── integration/
│   ├── tools/                   CLI tool integration tests (ctest)
│   │   ├── stlc/
│   │   │   ├── ir/              Golden IR JSON output per fixture
│   │   │   ├── sta/             Golden STA JSON output per fixture
│   │   │   └── emit/            All --emit targets on representative subset
│   │   ├── ista/                STA instantiation tests
│   │   ├── psta/                STA text parsing tests
│   │   ├── xfta/                FTA evaluation tests (--rng)
│   │   └── e2e/                 Full pipeline: stlc → ista → xfta → psta
│   └── modules/                 Python orchestration tests (pytest)
│       ├── conftest.py          Build dir discovery, engine fixture
│       └── test_smoke.py        Binding imports, compilation, pipeline
│
└── units/
    ├── libs/                    C++ library unit tests (ctest)
    │   └── autocog/compiler/stl/parser/
    │       ├── driver.cxx       Parser unit test driver
    │       └── inputs/          Grammar production test fixtures (JSON)
    └── modules/                 Python module unit tests (pytest)
        └── autocog/             (future)
```

## Test Frameworks

- **ctest** — C++ tests: tool integration, golden output diffs, parser units
- **pytest** — Python tests: binding smoke, orchestration, channels/clauses

## Running Tests

### C++ (ctest)

```bash
cd builds/autocog-dbg
cmake --build . -j4
ctest -j4 --timeout 30
```

### Python (pytest)

```bash
pytest tests/integration/modules tests/units/modules -v
```

## Golden Output Procedures

Golden output files (`.golden.json`, `.golden.txt`) are checked into the
repository alongside the CMakeLists.txt of the tool that produces them.
Tests diff the tool's actual output against the golden file.

### When a test fails

1. Run the failing test with `--output-on-failure` to see the diff
2. Determine if the change is expected (new feature, bug fix) or a regression
3. If expected: regenerate the golden output (see below)
4. If unexpected: fix the code

### Updating golden outputs

Golden output updates MUST be done in a dedicated commit with no other
changes, so they are easy to review and track:

```bash
# Regenerate all golden outputs
scripts/update-golden.sh

# Review the changes
git diff tests/

# Commit separately
git add tests/integration/tools/
git commit -m "Update golden test outputs"
```

### Adding a new test fixture

1. Create the `.stl` file in the appropriate `tests/fixtures/stl/` subdirectory
2. The CMakeLists in `stlc/ir/` and `stlc/sta/` auto-discover fixtures via GLOB
3. Generate golden outputs and commit them separately
4. If the fixture needs special flags (e.g., `-I` for imports), update the
   CMakeLists accordingly

## Fixture Categories

Each category builds on the previous:

- **language/** — Pure compilation tests. Every fixture needs at least
  `export ... as main` to produce STA. No channels needed.
- **flows/control + flows/return** — Adds flow statements and returns.
- **flows/data** — Adds basic channels (get, use). Uses only structures +
  simple channels + flows.
- **flows/calls** — Adds call channels. Uses structures + channels + flows.
- **flows/transform** — Adds clause modifiers. Uses structures + channels.
- **programs/** — Composes multiple features. The only place where
  expressions, symbols, AND complex channels appear together.

Tests in a category should NOT use features from a later category unless
specifically testing that interaction.

## Coverage Analysis

### Prerequisites

```bash
pip install -r tests/requirements.txt
```

### Running Coverage

```bash
tests/coverage.sh [BUILD_DIR]
```

This script:
1. Configures a coverage build (`-DCOVERAGE=ON`)
2. Builds and runs all C++ tests (ctest)
3. Generates C++ coverage via `gcovr` (JSON)
4. Runs Python tests with `pytest-cov` (JSON)
5. Generates `tests/coverage.md` — a combined markdown report

The report includes a global summary table, per-component C++ coverage,
expandable per-file detail, and Python module coverage.

### Coverage Targets

Coverage data guides test development. Priority targets are modules
below 70% coverage. The `clauses.py` module (data transformation
pipeline) and the Python orchestration path are the primary gaps.
