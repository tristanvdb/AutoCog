# C++ Tools

Low-level tools installed alongside the Python package. They operate on individual
pipeline stages and are primarily used for development, debugging, and scripting.

For most use cases the `autocog` CLI (which wraps these) is preferred.

## CLI conventions

These tools use **long options only** — there are no short aliases. Only `stlc`
takes positional arguments (its input `.stl` files); every other input and output
is a named option. The pipeline-artifact tools (`ista`, `xfta`, `psta`) require an
explicit output file; use `/dev/stdout` (and `/dev/stdin`) to pipe.

For `ista`, the `--syntax`, `--search`, and `--content` options accept **either a
file path or inline JSON** (e.g. `--content '{}'`).

## stlc — STL Compiler

Compile STL source files. At least one emit output is required; several may be
combined to emit multiple artifacts in one run.

```
stlc [emit outputs] [options] <file.stl> ...
```

| Flag | Description |
|------|-------------|
| `<file.stl> ...` | Input STL source files (positional) |
| `--ast FILE` | Emit the parsed AST |
| `--graph FILE` | Emit the instantiation graph |
| `--ir FILE` | Emit the intermediate representation |
| `--sta FILE` | Emit the compiled STA |
| `-I, --include PATH` | Import search path (repeatable; also `-I<path>`) |
| `-D, --define VAR[:TYPE]=VAL` | Define a compile-time variable (also `-D<def>`) |
| `--verbose [LEVEL]` | Log level (trace,debug,info,warn,error,critical) |
| `--version` | Show version |
| `--build-info` | Show build configuration |
| `--help` | Show help |

`stlc` keeps the standard compiler-style `-I`/`-D` short forms; all other options
are long-only. Each emit flag writes to its own file (use `/dev/stdout` for stdout),
and they compose:

### Define syntax

```bash
stlc -Dflag                  # bool, true
stlc -Dcount=5               # int
stlc -Dpi=3.14               # float
stlc -Dname="text"           # string
stlc -Dval:int=42            # explicit type
```

### Examples

```bash
stlc --sta /dev/stdout program.stl                       # STA to stdout
stlc --ir program.ir.json program.stl                    # IR to a file
stlc --ast a.json --sta s.json program.stl               # emit both in one run
stlc --sta program.sta.json -I share/library/stlib program.stl
stlc --sta /dev/null -Dmax_iter=5 program.stl            # with a compile-time define
```

## ista — STA Instantiation

Instantiate an STA prompt into an FTA. All inputs and `--fta` are required.

```
ista --sta FILE --prompt NAME --syntax F|JSON --search F|JSON --content F|JSON --fta FILE
```

| Flag | Description |
|------|-------------|
| `--sta FILE` | Compiled STA JSON |
| `--prompt NAME` | Prompt to instantiate |
| `--syntax F\|JSON` | Syntax config — a file path or inline JSON |
| `--search F\|JSON` | Search config — a file path or inline JSON |
| `--content F\|JSON` | Initial content — a file path or inline JSON |
| `--fta FILE` | Output FTA JSON (`/dev/stdout` for stdout) |
| `--verbose [LEVEL]` | Log level |
| `--version` / `--build-info` / `--help` | — |

### Examples

```bash
ista --sta program.sta.json --prompt main --syntax syntax.json --search search.json \
     --content '{}' --fta prompt.fta.json
ista --sta program.sta.json --prompt init_idea --syntax syntax.json --search search.json \
     --content input.json --fta /dev/stdout
```

## xfta — FTA Evaluation

Evaluate an FTA against a language model, producing an FTT (Finite Thought Tree —
the model's evaluation tree). `--fta` and `--ftt` are required.

```
xfta --fta FILE (--model FILE | --rng) --ftt FILE [--seed N] [--ctx N]
```

| Flag | Description |
|------|-------------|
| `--fta FILE` | Input FTA JSON |
| `--model FILE` | GGUF model file |
| `--rng` | Use built-in RNG model |
| `--ftt FILE` | Output FTT JSON (`/dev/stdout` for stdout) |
| `--seed N` | RNG seed (default: 42) |
| `--ctx N` | Model context size |
| `--verbose [LEVEL]` | Log level |
| `--version` / `--build-info` / `--help` | — |

The search parameters are embedded in the FTA (by `ista`), so `xfta` takes no
`--search`.

### Examples

```bash
xfta --rng --fta input.fta.json --ftt output.ftt.json                 # evaluate with RNG
xfta --model model.gguf --fta input.fta.json --ftt /dev/stdout        # real model, to stdout
```

## psta — FTT Parser

Score an FTT and parse it into a frame of field values, using the STA schema.
`--sta`, `--ftt`, `--prompt`, and `--frame` are required.

```
psta --sta FILE --ftt FILE --prompt NAME --frame FILE [--metric NAME]
```

| Flag | Description |
|------|-------------|
| `--sta FILE` | Compiled STA JSON |
| `--ftt FILE` | Input FTT JSON |
| `--prompt NAME` | Prompt the FTT corresponds to |
| `--frame FILE` | Output frame JSON (`/dev/stdout` for stdout) |
| `--metric NAME` | Scoring metric: `best` (default) |
| `--verbose [LEVEL]` | Log level |
| `--version` / `--build-info` / `--help` | — |

### Examples

```bash
psta --sta program.sta.json --ftt output.ftt.json --prompt main --frame result.json
```

## Pipeline Example

Chain the tools through files to run the full pipeline manually:

```bash
stlc --sta program.sta.json program.stl
ista --sta program.sta.json --prompt main --syntax syntax.json --search search.json \
     --content input.json --fta prompt.fta.json
xfta --model model.gguf --fta prompt.fta.json --ftt output.ftt.json
psta --sta program.sta.json --ftt output.ftt.json --prompt main --frame result.json
```

Or piped end-to-end via `/dev/stdin` and `/dev/stdout`:

```bash
stlc --sta program.sta.json program.stl
ista --sta program.sta.json --prompt main --syntax syntax.json --search search.json \
     --content input.json --fta /dev/stdout \
  | xfta --rng --fta /dev/stdin --ftt /dev/stdout \
  | psta --sta program.sta.json --ftt /dev/stdin --prompt main --frame /dev/stdout
```
