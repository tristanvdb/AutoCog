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

## Test Pipelines

Each pipeline that builds AutoCog has a top-level script under `tests/scripts/`
so it can be run locally with one command. The actual work lives in shared,
parameterized step scripts under `tests/scripts/steps/`. CI runs the same steps,
but calls them one per workflow step (so GitHub's UI shows where a failure
occurred) rather than going through the top script — so the orchestration (which
steps, in what order, with what options) is intentionally expressed in both
places. The logic itself is not duplicated; only the short call sequence is.

### Shared steps (`tests/scripts/steps/`)

Parameterized by environment (`BUILD_DIR`, `BUILD_TYPE`, `COVERAGE`, `COV`) and
venv-agnostic — they resolve `pip`/`python`/`gcovr` from `PATH`. Top scripts put
a venv's `bin` on `PATH`; CI uses the system interpreter.

- `build.sh` — install AutoCog (`--no-deps`) for the given `BUILD_TYPE`/`COVERAGE`
- `ctest.sh` — C++ tests, accumulating `.gcda`
- `pytest.sh` — Python tests; `COV=1` enables `pytest-cov`
- `cxx-report.sh` — `gcovr` → `coverage_cxx.json` (coverage builds)
- `report.sh` — combine into `tests/coverage.md` (coverage builds)

### Top scripts

```bash
tests/scripts/tests-coverage.sh [BUILD_DIR] [VENV_DIR]   # Debug + coverage → coverage.md
tests/scripts/tests-release.sh  [BUILD_DIR] [VENV_DIR]   # Release, ctest + pytest, no coverage
tests/scripts/check-release.sh  [BUILD_DIR] [VENV_DIR]   # user install .[server,validate] + smoke
```

Argument semantics (all optional):
- `BUILD_DIR` — cmake build dir. If supplied, reused across runs (ccache stays
  hot) and wiped at the start of each run; if omitted, a temp dir removed on exit.
- `VENV_DIR` — a venv you manage (reused, not removed). If omitted, a temp venv
  is created and removed on exit. The test scripts seed it from the `test`
  dependency-group; `check-release.sh` leaves it bare and installs via the extras.

### Install smoke test

`tests/scripts/smoke.sh` verifies an installation without any test framework
(imports the package, runs `autocog --version` and `stlc --version`). Users can
run it to confirm their own install:

```bash
pip install "autocog[server,validate]"
tests/scripts/smoke.sh
```

### Dependencies

The `test` dependency-group in `pyproject.toml` is the single source of truth
for the test environment. To set up a venv by hand (deps only, no build):

```bash
pip install --group test          # from the repo root (pip >= 25.1)
```

It carries the harness tooling (pytest, gcovr, deepdiff, …) plus the runtime
extras the suite exercises (fastapi/uvicorn, jsonschema). A drift check,
`tests/scripts/check_dep_group_drift.py`, fails if the group stops mirroring the
`server`/`validate` extras.

### CI jobs (`.github/workflows/ci.yaml`)

1. `check-deps` — dependency-group drift check (no build)
2. `check-release` — user-POV install of `.[server,validate]` + smoke
3. `tests-release` — Release build, ctest + pytest
4. `tests-coverage` — Debug + coverage, ctest + pytest, report; publishes the
   live `master` badges (grey when stats could not be produced)

The "latest release" badges are stamped separately by `release.yaml`, only after
a release fully succeeds.
