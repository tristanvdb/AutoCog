# Python API

```python
import autocog
```

## Program Management

### autocog.compile

Compile an STL source file into a Program.

```python
prog = autocog.compile(filepath, includes=None, entry_points=None)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `filepath` | str | Path to STL source file |
| `includes` | list[str] | Import search paths (stdlib added automatically) |
| `entry_points` | list[str] | Export names (default: `["main"]`) |

Returns: `Program`

### autocog.load

Load a pre-compiled STA JSON file.

```python
prog = autocog.load(filepath)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `filepath` | str | Path to STA JSON file |

Returns: `Program`

### Program

Wraps a compiled or loaded STA program.

| Property/Method | Returns | Description |
|-----------------|---------|-------------|
| `prog.sta` | dict | Full STA as a Python dict |
| `prog.entry_points` | dict | `{name: {prompt, inputs, outputs}}` |
| `prog.prompts` | dict | Prompt definitions |
| `prog.python_imports` | dict | `{extern_name: {file, target}}` |
| `prog.entry_prompt(entry="main")` | str | Prompt name for an entry point |
| `prog.input_schema(entry="main")` | dict | Input schema for an entry point |
| `prog.output_schema(entry="main")` | dict | Output schema for an entry point |
| `prog.prompt_channels(name)` | list | Channels for a prompt |
| `prog.prompt_flows(name)` | dict | Flow definitions for a prompt |

## Execution

### Engine

Local execution engine. Holds a model and syntax configuration.

```python
engine = autocog.Engine(model=None, syntax=None, search=None, n_ctx=4096)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `model` | str or None | Path to GGUF model file. `None` for RNG model. |
| `syntax` | str | Path to syntax JSON file (required) |
| `search` | str | Path to search config JSON file (required) |
| `n_ctx` | int | Model context size (default: 4096) |

#### engine.run

Run a program to completion.

```python
result = engine.run(program, entry="main", externals=None, max_steps=100, recorder=None, **inputs)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `program` | Program | Compiled program |
| `entry` | str | Entry point name |
| `externals` | dict | `{name: callable}` for extern channels |
| `max_steps` | int | Safety limit on prompt iterations |
| `recorder` | Recorder or None | Optional artifact recorder (see `autocog.recorder.Recorder`) |
| `**inputs` | | Input values (passed to `get` channels) |

Returns: result value (str, dict, or nested structure depending on the return flow).

`engine.run_async(program, entry="main", externals=None, **inputs)` is the awaitable
variant, for programs whose extern callables are coroutines.
`engine.set_seed(seed)` sets the RNG seed of the underlying (or built-in RNG) model.

#### engine.evaluate_prompt

Evaluate a single prompt. This is the dispatch point for local vs remote execution.

```python
frame = engine.evaluate_prompt(program, prompt_name, content, record_kinds=None)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `program` | Program | Compiled program |
| `prompt_name` | str | Mangled prompt name |
| `content` | dict | Resolved channel values |
| `record_kinds` | set or None | Artifact kinds to also return (`fta`, `ftt`, `frame`, `input`) |

Returns: dict of parsed field values (the "frame"), or `(frame, artifacts)` when
`record_kinds` is set. Internally this runs instantiate → evaluate → walk against the
C++ runtime and backend (see [Runtime Semantics](../compiler/runtime-semantics.md)).

### RemoteEngine

Drop-in replacement for `Engine` that dispatches evaluation over HTTP to a level-2 RPC server.

```python
engine = autocog.RemoteEngine(server_url, poll_interval=0.5, timeout=300)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `server_url` | str | Base URL of the RPC server |
| `poll_interval` | float | Seconds between status polls |
| `timeout` | float | Maximum seconds to wait |

Provides the same `run()` and `evaluate_prompt()` methods as `Engine`. Channel resolution happens locally; only evaluation is remote.

```python
# Local development
engine = autocog.Engine(syntax="syntax.json", search="search.json")
result = engine.run(prog, query="test")

# Deploy to remote GPU — same code, different engine
engine = autocog.RemoteEngine("http://gpu-box:8080")
result = engine.run(prog, externals=externals, query="test")
```

### RemoteBackend

Drop-in replacement for `Engine` that dispatches **FTA evaluation** over HTTP to a
level-3 backend server. Channel resolution *and* instantiation happen locally (hence
`syntax`/`search` are required); only the FTA→FTT evaluation is remote.

```python
engine = autocog.RemoteBackend(server_url, syntax=None, search=None,
                               poll_interval=0.5, timeout=300)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `server_url` | str | Base URL of the backend server |
| `syntax` | str | Path to syntax JSON file (required — local instantiation) |
| `search` | str | Path to search config JSON file (required — local instantiation) |
| `poll_interval` | float | Seconds between status polls |
| `timeout` | float | Maximum seconds to wait |

Provides the same `run()` / `evaluate_prompt()` interface as `Engine`. This is the
third remoting level, alongside `remote_run` (level 1, full app) and `RemoteEngine`
(level 2, per-prompt).

### autocog.remote_run

Level-1 convenience — run against a serve endpoint with no local program needed.

```python
result = autocog.remote_run(server_url, entry="main", **inputs)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `server_url` | str | Base URL of the serve server |
| `entry` | str | Entry point name |
| `poll_interval` | float | Seconds between polls (default: 0.5) |
| `timeout` | float | Max wait in seconds (default: 300) |
| `**inputs` | | Input values |

Returns: result from the program.

### Context

Execution state for a single program run. Used internally by `engine.run()` but can be driven manually for step-by-step control.

```python
ctx = autocog.Context(program, engine, prompt, inputs, externals)
while not ctx.done:
    ctx.step()
result = ctx.result
```

| Attribute | Type | Description |
|-----------|------|-------------|
| `ctx.done` | bool | True when execution is complete |
| `ctx.result` | any | Final result (available when done) |
| `ctx.prompt` | str | Current prompt name |
| `ctx.frames` | dict | `{prompt_name: frame}` for completed prompts |

## Packaging

### autocog.stapp.pack

Create a `.stapp` package from an STL program.

```python
from autocog.stapp import pack

pack(stl_path, include_paths, output_path,
     no_compile=False, no_source=False, vendor_stdlib=False,
     schema_override=None)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `stl_path` | str | Path to main STL file |
| `include_paths` | list[str] | Import search directories |
| `output_path` | str | Output .stapp file path |
| `no_compile` | bool | Skip STA compilation |
| `no_source` | bool | Strip STL source |
| `vendor_stdlib` | bool | Bundle stdlib |
| `schema_override` | str or None | Path to schema override JSON |

### autocog.stapp.load_stapp

Load a `.stapp` package for execution.

```python
from autocog.stapp import load_stapp

prog, manifest, temp_dir, include_paths = load_stapp(stapp_path, recompile=False)
```

Returns a tuple. The caller should clean up `temp_dir` when done:

```python
import shutil
try:
    result = engine.run(prog, externals=load_externals(prog, include_paths), **inputs)
finally:
    shutil.rmtree(temp_dir, ignore_errors=True)
```
