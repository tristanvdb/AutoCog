# ⚙️ AutoCog - Automaton & Cognition

[![CI](https://github.com/tristanvdb/AutoCog/actions/workflows/ci.yaml/badge.svg?branch=master)](https://github.com/tristanvdb/AutoCog/actions/workflows/ci.yaml)
[![Release](https://github.com/tristanvdb/AutoCog/actions/workflows/release.yaml/badge.svg)](https://github.com/tristanvdb/AutoCog/actions/workflows/release.yaml)
[![Latest release](https://img.shields.io/github/v/release/tristanvdb/AutoCog)](https://github.com/tristanvdb/AutoCog/releases/latest)
![Release date](https://img.shields.io/github/release-date/tristanvdb/AutoCog)
[![Downloads](https://img.shields.io/github/downloads/tristanvdb/AutoCog/total)](https://github.com/tristanvdb/AutoCog/releases)
![License](https://img.shields.io/github/license/tristanvdb/AutoCog)
![Python](https://img.shields.io/badge/python-3.10%2B-blue)
![ctest](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-ctest.json)
![pytest](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-pytest.json)
![C++ coverage](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-cxx-coverage.json)
![Python coverage](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-py-coverage.json)

**AutoCog** explores mechanisms to build automata that control applications driven by auto-regressive language models. We define a programming model called **Structured Thoughts**, with a language (STL) that compiles to executable automata.

## Quick Start

```bash
git clone --recursive https://github.com/LLNL/AutoCog
cd AutoCog
pip install .
```

For server functionality (FastAPI + uvicorn):

```bash
pip install ".[server]"
```

Vendored dependencies (RE-flex, llama.cpp) are built automatically if not found on the system.

## What is Structured Thoughts?

Structured Thoughts is a programming model where:
- **Prompts** are building blocks (like functions in traditional programming)
- **Automata** ensure completions can be parsed to extract structured data
- **Branching** between prompts is controlled by the language model
- **Dataflow** is statically defined and executed when instantiating automata

Example — Multiple Choice Question with Chain of Thought:

```
record thought {
  is text<length=20>;
  annotate f"a short text representing a single thought";
}

prompt main {
  is {
    topic is text<length=20>;
    question is text<length=50>;
    choices[4] is text<length=40>;
    work[1:10] is thought;
    answer is select(choices);
  }
  channel {
    topic get topic;
    question get question;
    choices get choices;
  }
  return {
    use answer;
  }
  annotate {
    _ as "You are answering a multiple choice questionnaire.";
    question as "the question that you have to answer";
    choices as "the four possible choices, only one is correct";
    work as "show your work step-by-step";
    answer as "you pick the index of the choice that best answers the question";
  }
}
```

## CLI

AutoCog provides a command-line interface with subcommands:

```bash
# Compile STL to STA
autocog compile --stl program.stl -o program.sta.json

# Run a program
autocog run --stl program.stl --rng --input '{"topic":"Science","question":"2+2?","choices":["3","4","5","6"]}'
autocog run --sta program.sta.json --model model.gguf --input data.json

# Package as a .stapp (Structured Thought App)
autocog pack --stl program.stl -I includes/ -o app.stapp

# Run from a .stapp
autocog run --app app.stapp --model model.gguf --input '{"key":"value"}'

# Serve with web UI
autocog serve --app app.stapp --model model.gguf --port 8080

# Prompt evaluation server (RPC)
autocog rpc --sta program.sta.json --model model.gguf --port 8080

# Inference server (backend)
autocog backend --model model.gguf --port 8080
```

Use `--rng` instead of `--model` for testing without a real model.

## .stapp Packaging

Programs can be packaged as `.stapp` files (zip) for distribution:

```bash
autocog pack --stl writer.stl -I src/ -o writer.stapp
autocog pack --stl writer.stl -I src/ --no-source -o writer.stapp   # strip source
autocog pack --stl writer.stl -I src/ --vendor-stdlib -o writer.stapp  # self-contained
```

A `.stapp` bundles the compiled STA, source (optional), Python externals, and STL imports with a manifest containing entry point schemas.

## Containers

Build container variants for deployment:

```bash
docker build --target serve   -f dockerfiles/ubuntu.df -t autocog:serve .
docker build --target backend -f dockerfiles/ubuntu.df -t autocog:backend .
docker build --target rpc     -f dockerfiles/ubuntu.df -t autocog:rpc .
docker build --target run     -f dockerfiles/ubuntu.df -t autocog:run .
```

| Variant | Purpose | Default |
|---------|---------|---------|
| `serve` | Full app server with web UI | `autocog serve` |
| `rpc` | Prompt evaluation server | `autocog rpc` |
| `backend` | Inference server (FTA evaluation) | `autocog backend --rng` |
| `run` | Batch execution | `autocog run` |

Example with docker compose:

```bash
docker compose -f dockerfiles/compose.yaml up
```

See [dockerfiles/compose.yaml](dockerfiles/compose.yaml) for a multi-container example.

## Examples

The [share/demos/mcq/](./share/demos/mcq/) directory contains Multiple Choice Question examples demonstrating various thought patterns (basic select/repeat, chain of thought, hypothesis, iterative reflection). See [share/demos/mcq/README.md](./share/demos/mcq/README.md).

The [share/demos/story-writer/](./share/demos/story-writer/) directory contains a multi-prompt story writer with loops, Python externals, and templated content. See [share/demos/story-writer/README.md](./share/demos/story-writer/README.md).

## Syntax Highlighting

### gedit
```bash
mkdir -p ~/.local/share/libgedit-gtksourceview-300/language-specs
cp share/syntax-highlight/gedit/stl.lang ~/.local/share/libgedit-gtksourceview-300/language-specs
```

### VSCode
```bash
mkdir -p ~/.vscode/extensions/stl-language
cp -r share/syntax-highlight/vscode/* ~/.vscode/extensions/stl-language
```

## Contributing

Contributions are welcome!

**Key Rule**: **Linear git history** (no merge commits). Only the `master` branch has stable commits; other branches may be rebased without notice.

## License

AutoCog is distributed under the terms of the **Apache License (Version 2.0) with LLVM exceptions**.

All new contributions must be made under Apache-2.0 license (with LLVM exceptions).

SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

LLNL-CODE-850523
