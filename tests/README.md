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
│   │   │   ├── search/          Search constructs
│   │   │   └── vocab/           Vocabularies (vocab, tokenize, regex, set algebra)
│   │   ├── flows/               Execution semantics (need runtime to test)
│   │   │   ├── control/         Flow statements (simple, loop with limit)
│   │   │   ├── return/          Return statements (simple, labeled)
│   │   │   ├── data/            Channels: input (get), dataflow (use), self-ref
│   │   │   ├── calls/           Call channels (basic, mapped, bind)
│   │   │   └── transform/       Clause modifiers (ravel, wrap, prune)
│   │   ├── programs/            Composition of multiple features
│   │   └── errors/              Invalid STL → expected diagnostics (check_error_output.py)
│   └── fta/                     FTA JSON fixtures (pre-tokenized)
│
├── integration/
│   ├── tools/                   CLI tool integration tests (ctest)
│   │   ├── stlc/
│   │   │   ├── ir/              Golden IR JSON output per fixture
│   │   │   ├── sta/             Golden STA JSON output per fixture
│   │   │   └── emit/            All emit formats on a representative subset
│   │   ├── ista/                STA instantiation tests
│   │   ├── psta/                FTT scoring → frame tests (STA + FTT → frame)
│   │   ├── xfta/                FTA evaluation tests (--rng)
│   │   └── e2e/                 Full pipeline: stlc → ista → xfta → psta
│   ├── bindings/                C++ binding integration tests (pytest)
│   │   └── test_bindings.py     compile → instantiate → evaluate → walk via bindings
│   └── modules/                 Python orchestration tests (pytest)
│       ├── conftest.py          Build dir discovery, engine fixture
│       ├── test_smoke.py        Binding imports, compilation, pipeline
│       ├── test_channels.py     Channel resolution (get/use/call, clauses)
│       ├── test_error_contract.py   Error types and messages
│       ├── test_json_logging.py     --json NDJSON logging
│       └── test_cli_extra.py        Additional CLI command coverage
│
└── units/
    ├── bindings/                Datastore binding units (pytest)
    │   └── test_store.py        Six-verb load/read/get/store/dump/release round-trips
    ├── libs/                    C++ library unit tests (ctest)
    │   └── autocog/
    │       ├── compiler/stl/parser/   driver.cxx + inputs/*.json (grammar productions)
    │       ├── data/                  registry + artifact smoke drivers
    │       └── utilities/             backtrace driver
    └── modules/                 Python module unit tests (pytest)
        └── autocog/             test_clauses.py
```

## Test Frameworks

- **ctest** — C++ tests: tool integration, golden output diffs, parser units
- **pytest** — Python tests: binding smoke, orchestration, channels/clauses

## Running Tests

### C++ (ctest)

```bash
cmake --build build -j4
ctest --test-dir build -j4 --timeout 30
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
# Regenerate all golden outputs (validates each against its schema before writing)
BUILD_DIR=build tests/scripts/update-golden.sh           # all targets
BUILD_DIR=build tests/scripts/update-golden.sh --target sta --dry-run

# Review the changes
git diff tests/

# Commit separately
git add tests/integration/tools/
git commit -m "Update golden test outputs"
```

`update-golden.sh` never writes a golden that fails its schema — it shells out to
`golden_compare.py --validate-only`. Pass `--target sta|ir|e2e|all` to scope it and
`--dry-run` to preview. The `e2e` target reruns the full
`stlc → ista → xfta --rng --seed 42 → psta` pipeline per fixture, validating the
intermediate FTA and FTT against their schemas before writing the frame golden.

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
tests/scripts/tests-release.sh  [BUILD_DIR] [VENV_DIR]   # Release (portable), ctest + pytest, no coverage
tests/scripts/tests-native.sh   [BUILD_DIR] [VENV_DIR]   # Release, host-tuned (native/CUDA) + parallel
tests/scripts/check-release.sh  [BUILD_DIR] [VENV_DIR]   # user install .[server,validate] + smoke (portable)
tests/scripts/check-native.sh   [BUILD_DIR] [VENV_DIR]   # user install + smoke, host-tuned + parallel
```

The `tests-*` scripts run `ctest`, so they build via `pip` with
`-DAUTOCOG_BUILD_TESTS=ON` (a scikit-build/`pip` build configures **no** tests
otherwise; a direct `cmake` build configures them by default). `tests-release`
is portable (`AUTOCOG_TUNED=OFF`); `tests-native` is host-tuned and parallel,
for validating one machine (e.g. CUDA/ARM) rather than a portable wheel.

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

### Branching model

Work flows `develop` → `candidate` → `master` (via release tag):

- **`develop` / `*-dev`** — push here for iteration. CI runs a *basic* subset
  (git-guard, check-deps, check-release, tests-coverage) and uploads
  `coverage.md` as an artifact. A red run (e.g. the guard) just means rebase
  before promoting; nothing depends on develop.
- **`candidate`** — push here to validate a release candidate. CI runs the
  *full* set (adds tests-release) and publishes the live `candidate` badges.
- **`master`** — never pushed to directly in normal flow. A release tag, on
  success, fast-forwards `master` to the tagged commit (so master == latest
  release). Tags are cut from `candidate`.

`git-guard` enforces linear history: every branch must descend from `master`
with no merge commits in `master..HEAD`.

### CI jobs (`.github/workflows/ci.yaml`)

Triggered on push to `develop`, `*-dev`, and `candidate` (no PR triggers).

1. `git-guard` — linear-history check (descends from master, no merge commits)
2. `check-deps` — dependency-group drift check (no build)
3. `check-release` — user-POV install of `.[server,validate]` + smoke
4. `tests-coverage` — Debug + coverage, ctest + pytest, report + artifact;
   on `candidate`, publishes the live `candidate` badges (green/red by
   threshold, grey when stats could not be produced)
5. `tests-release` — Release build, ctest + pytest (**candidate only**)
6. `ci-status` — writes the `candidate` status badge (short hash; green only
   if every job succeeded)

The `latest release` badges (and the master fast-forward) are handled by
`release.yaml`, only after a release fully succeeds.

### Release pipeline (`release.yaml`)

Triggered by pushing a `v*` tag. Jobs run roughly in dependency order:

1. `verify` — gates the tag: it must match the `VERSION` file, be contained in
   `candidate`, and have a fully green CI run for its commit. Also a major-version
   `NotImplementedError`-site check, and an *informational* report of any
   `share/schemas/` changes since the previous tag (no longer a hard gate).
2. `coverage-gate` — downloads the CI coverage artifact for the commit and
   re-asserts `pass=true` (defense-in-depth; thresholds are enforced upstream in
   `coverage_report.py`).
3. `wheels` / `sdist` — build distributables with `cibuildwheel` (portable:
   `AUTOCOG_TUNED=OFF`) and an sdist.
4. `demo` — installs the built wheel and packs the story-writer demo into a `.stapp`.
5. `containers` — build & push the `run`/`serve`/`rpc`/`backend` images to GHCR.
6. `release` — create the GitHub Release with wheels, sdist, demo, and coverage.
7. `publish-badges` — stamp the live `release` badges from the CI coverage artifact.
8. `push-master` — fast-forward `master` to the tag (fails on purpose if the tag
   touched `.github/workflows/**`, since `GITHUB_TOKEN` can't push those — a human
   must fast-forward manually).
9. `release-status` — writes the `release` status badge (version, green only if
   every needed job succeeded).

## Helper scripts (`tests/scripts/`)

The pipeline orchestration (top scripts + `steps/`) is described under
[Test Pipelines](#test-pipelines). The remaining standalone helpers:

| Script | Purpose |
|--------|---------|
| `golden_compare.py` | Compare tool output to a golden with a structural (deepdiff) diff, and validate against the matching `share/schemas/` schema. Exit codes: 0 match, 1 structural mismatch, 2 schema failure. `--validate-only` / `--no-validate`. The ctest golden tests call this. |
| `update-golden.sh` | Regenerate goldens, schema-validating each before writing. `sta`/`ir` from `stlc` output; `e2e` from the full `stlc→ista→xfta→psta` pipeline (validating the FTA + FTT too). `--target sta\|ir\|e2e\|all`, `--dry-run`. Needs `BUILD_DIR`. |
| `coverage_report.py` | Turn gcovr + pytest-cov JSON into `tests/coverage.{md,json}`; the single source of truth for the template-file classification, coverage thresholds, and the pass/fail the CI badges and release gate consume. Nonzero exit if a gating category misses threshold. |
| `check_error_output.py` | For negative STL fixtures: assert `stlc`'s combined output contains each `// expect-error: <substr>` directive; an `internal error` always fails. |
| `check_stlc_behavior.py` | For flag-driven `stlc` invocations: assert exit code + diagnostics against an expected `ok` / `warning <substr>` / `error <substr>` outcome. |
| `check_dep_group_drift.py` | Fail if the `server`/`validate` extras are not mirrored (name + specifier) into the `test` dependency-group in `pyproject.toml`. Run by the `check-deps` CI job. |
| `fuzz.sh` | Run an STL program under many random RNG seeds (`autocog run --rng`) to shake out instantiation/parse crashes. Not part of CI; a manual robustness tool. |
| `smoke.sh` | Framework-free install check (import, `autocog --version`, `stlc --version`). Run by the `check-release` CI job and usable directly by end users. |
