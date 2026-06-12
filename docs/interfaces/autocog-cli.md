# autocog CLI

The `autocog` command is installed via `pip install autocog`. It provides subcommands for compilation, packaging, execution, and serving.

## Global Options

```
autocog --version              Show version
autocog --build-info           Show build configuration (compiler, CUDA, etc.)
autocog --json                 Emit log records as NDJSON (ECS-flavored) on stderr
autocog --json-log-file PATH   With --json: write log records to PATH instead of stderr
```

`--json` / `--json-log-file` are global flags placed before the subcommand.

## autocog compile

Compile an STL program to STA JSON.

```
autocog compile --stl <file> [-I <path>]... [-o <output>]
```

| Flag | Description |
|------|-------------|
| `--stl FILE` | STL source file (required) |
| `-I, --include PATH` | Import search path (repeatable) |
| `-o, --output FILE` | Output file (default: stdout) |

The compiler automatically appends the installed stdlib as the lowest-priority include path.

**Examples:**

```bash
autocog compile --stl program.stl                     # STA to stdout
autocog compile --stl program.stl -o program.sta.json  # STA to file
autocog compile --stl program.stl -I src/              # with include path
```

## autocog pack

Package an STL program into a `.stapp` file (zip archive).

```
autocog pack --stl <file> [-I <path>]... -o <output.stapp>
             [--no-compile] [--no-source] [--vendor-stdlib] [--schema <file>]
```

| Flag | Description |
|------|-------------|
| `--stl FILE` | STL source file (required) |
| `-I, --include PATH` | Import search path (repeatable) |
| `-o, --output FILE` | Output .stapp file (required) |
| `--no-compile` | Omit pre-compiled STA (source only) |
| `--no-source` | Strip STL source from package (IP protection) |
| `--vendor-stdlib` | Bundle stdlib files into the package |
| `--schema FILE` | Schema override JSON (default: auto-detect `schema.json` next to STL) |

The pack step compiles the STL, collects app-specific externals (Python files) and STL imports, generates a manifest with entry point schemas, and zips everything.

**Examples:**

```bash
autocog pack --stl writer.stl -I src/ -o writer.stapp
autocog pack --stl writer.stl -I src/ --no-source -o writer.stapp
autocog pack --stl writer.stl -I src/ --vendor-stdlib -o writer.stapp
```

## autocog run

Run a program to completion.

```
autocog run (--stl <file> | --sta <file> | --app <file>)
            [-I <path>]... [--recompile]
            (--model <file> | --rng) [--syntax <file>] [--search <file>] [--ctx N]
            [--input <json>] [--entry <name>] [--max-steps N] [--seed N]
            [--record KINDS] [--record-path DIR] [--no-schema-check]
            [-o <output>] [-v]
```

| Flag | Description |
|------|-------------|
| `--stl FILE` | STL source file to compile and run |
| `--sta FILE` | Pre-compiled STA JSON file |
| `--app FILE` | .stapp package file |
| `-I, --include PATH` | Import search path (repeatable) |
| `--recompile` | Force recompilation from source (.stapp only) |
| `--model FILE` | GGUF model file |
| `--rng` | Use built-in RNG model (for testing) |
| `--syntax FILE` | Syntax description JSON (default: `share/syntax/default.json`) |
| `--search FILE` | Search config JSON (default: `share/search/default.json`) |
| `--ctx N` | Model context size (default: 4096) |
| `--input JSON` | Input data — inline JSON string or path to a JSON file |
| `--entry NAME` | Entry point name (default: `main`) |
| `--max-steps N` | Maximum prompt steps (default: 100) |
| `--seed N` | RNG seed (default: 42) |
| `-o, --output FILE` | Output file (default: stdout) |
| `--record KINDS` | Record artifacts: comma-separated subset of `input,frame,fta,ftt` |
| `--record-path DIR` | Directory for recorded artifacts (default: temp dir) |
| `--no-schema-check` | Disable schema validation of compiled artifacts |
| `-v, --verbose` | Show step-by-step progress |

One of `--stl`, `--sta`, or `--app` is required. One of `--model` or `--rng` is required.

By default the compiled STA is validated against its schema and a schema
violation is a **fatal error**. Pass `--no-schema-check` to skip validation
entirely (validation also no-ops if `jsonschema` is not installed).

**Examples:**

```bash
# From STL source
autocog run --stl select.stl --rng --input '{"topic":"Sci","question":"2+2?","choices":["3","4","5","6"]}'

# From pre-compiled STA
autocog run --sta program.sta.json --model model.gguf --input data.json

# From .stapp package
autocog run --app writer.stapp --model model.gguf --input '{"query":"test","age":"5"}'

# With verbose output
autocog run --stl writer.stl -I src/ --rng --input '{"query":"test","age":"3"}' -v
```

## autocog serve

Start a full app server with auto-generated web UI.

```
autocog serve (--stl <file> | --sta <file> | --app <file>)
              [-I <path>]... [--recompile]
              (--model <file> | --rng) [--syntax <file>] [--ctx N]
              [--host HOST] [--port PORT]
```

| Flag | Description |
|------|-------------|
| `--stl/--sta/--app` | Program source (same as `run`) |
| `--model/--rng` | Model selection (same as `run`) |
| `--syntax FILE` | Syntax description JSON (default: `share/syntax/default.json`) |
| `--search FILE` | Search config JSON (default: `share/search/default.json`) |
| `--host HOST` | Bind address (default: `0.0.0.0`) |
| `--port PORT` | Bind port (default: `8080`) |

> **Security:** `serve`, `rpc`, and `backend` bind `0.0.0.0` (all interfaces) by
> default and expose **unauthenticated** endpoints. These are Flask dev servers,
> not hardened services — bind `--host 127.0.0.1` for local use, and put them
> behind your own authentication/proxy before exposing them on a network.

The server provides:
- `GET /` — auto-generated HTML form from input schema
- `POST /run` — submit `{entry, inputs}`, returns `{request_id}`
- `GET /status/{request_id}` — poll for result
- `GET /schema` — entry points with input/output schemas

**Example:**

```bash
autocog serve --app writer.stapp --model model.gguf --port 8080
```

## autocog rpc

Start a prompt evaluation server (Remote Prompt Call).

```
autocog rpc (--stl <file> | --sta <file> | --app <file>)
            [-I <path>]... [--recompile]
            (--model <file> | --rng) [--syntax <file>] [--ctx N]
            [--host HOST] [--port PORT]
```

The server provides:
- `POST /prompt` — submit `{prompt, content}`, returns `{request_id}`
- `GET /status/{request_id}` — poll for result
- `GET /schema` — entry points with input/output schemas

The client does channel resolution locally and sends fully-resolved content. The server handles instantiation, evaluation, and parsing.

**Example:**

```bash
autocog rpc --sta program.sta.json --model model.gguf --port 8080
```

## autocog backend

Start an inference server (FTA evaluation).

```
autocog backend [--model <file>] [--rng] [--ctx N] [--host HOST] [--port PORT]
```

| Flag | Description |
|------|-------------|
| `--model FILE` | GGUF model file |
| `--rng` | Use built-in RNG model |
| `--ctx N` | Model context size (default: 4096) |
| `--host HOST` | Bind address (default: `0.0.0.0`) |
| `--port PORT` | Bind port (default: `8080`) |

The backend takes no `--search` flag — search parameters are embedded in each FTA
it receives.

The server provides:
- `POST /evaluate` — submit `{fta: {...}}`, returns `{request_id}`
- `GET /status/{request_id}` — poll for result
- `GET /models` — list loaded model tags

The `rng` model tag is always available, even with no `--model` flag.

**Example:**

```bash
autocog backend --model model.gguf --port 8080
autocog backend --rng   # testing without a model
```
