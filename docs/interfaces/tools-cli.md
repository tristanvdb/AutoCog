# C++ Tools

Low-level tools installed alongside the Python package. These operate on individual pipeline stages and are primarily used for development, debugging, and scripting.

For most use cases, the `autocog` CLI (which wraps these) is preferred.

## stlc — STL Compiler

Compile STL source files to various intermediate representations.

```
stlc [options] [files...]
```

| Flag | Description |
|------|-------------|
| `-i, --input FILE` | Input STL file |
| `-o, --output FILE` | Output file (default: stdout) |
| `-I, --include PATH` | Import search path (repeatable) |
| `-D, --define VAR=VAL` | Define a compile-time variable |
| `-e, --emit TARGET` | Output format (default: `sta`) |
| `-V, --verbose` | Verbose output |
| `-v, --version` | Show version |

### Emit targets

| Target | Description |
|--------|-------------|
| `ast` | Abstract syntax tree (parse output) |
| `symbols` | Symbol table |
| `globals` | Global scope after resolution |
| `graph` | Instantiation graph |
| `ir` | Intermediate representation |
| `sta` | Structured Thought Automaton (final compiled output) |

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
stlc program.stl                              # STA to stdout
stlc --emit ir program.stl                    # IR to stdout
stlc --emit sta -I share/library/stlib program.stl -o program.sta.json
stlc -Dmax_iter=5 program.stl                 # with compile-time define
```

## ista — STA Instantiation

Instantiate an STA prompt into an FTA (text-level automaton).

```
ista [options] <sta.json>
```

| Flag | Description |
|------|-------------|
| `-p, --prompt NAME` | Prompt name (default: first entry point) |
| `-s, --syntax FILE` | Syntax description JSON |
| `-d, --data FILE` | Initial content JSON (default: `{}`) |
| `-o, --output FILE` | Output FTA JSON (default: stdout) |
| `-t, --text` | Print FTA as formatted text instead of JSON |

### Examples

```bash
ista program.sta.json                         # default entry point, JSON output
ista --text program.sta.json                  # human-readable text
ista -p init_idea -d input.json program.sta.json   # specific prompt with data
ista -s syntax.json -o output.fta.json program.sta.json
```

## xfta — FTA Evaluation

Evaluate an FTA against a language model, producing an FTT (execution trace).

```
xfta [options] <fta.json> ...
```

| Flag | Description |
|------|-------------|
| `-m, --model PATH` | GGUF model file |
| `-r, --rng` | Use built-in RNG model |
| `-c, --ctx SIZE` | Model context size |
| `-b, --best` | Print best-path text to stdout (no FTT output) |
| `-v, --verbose` | Verbose output |

### Examples

```bash
xfta --rng --best input.fta.json              # evaluate with RNG, print text
xfta --model model.gguf --best input.fta.json  # evaluate with real model
xfta --rng input.fta.json > output.ftt.json    # full FTT output
```

## psta — STA Text Parser

Parse text (from model output) back into structured field values using the STA schema.

```
psta [options] <sta.json>
```

| Flag | Description |
|------|-------------|
| `-p, --prompt NAME` | Prompt name (default: first entry point) |
| `-s, --syntax FILE` | Syntax description JSON |
| `-i, --input FILE` | Text to parse (default: stdin) |
| `-o, --output FILE` | Output JSON (default: stdout) |

### Examples

```bash
echo "field text here" | psta program.sta.json             # parse from stdin
psta -i model_output.txt -o parsed.json program.sta.json   # file to file
psta -p init_idea program.sta.json < text.txt              # specific prompt
```

## Pipeline Example

Chain the tools to execute the full pipeline manually:

```bash
# Compile
stlc program.stl -o program.sta.json

# Instantiate
ista -d input.json -o prompt.fta.json program.sta.json

# Evaluate
xfta --model model.gguf --best prompt.fta.json > output.txt

# Parse
psta program.sta.json < output.txt > result.json
```

Or in a single pipeline:

```bash
ista -d input.json program.sta.json | xfta --rng --best /dev/stdin | psta program.sta.json
```
