# ⚙️ AutoCog - Automaton & Cognition

[![release](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-release-status.json)](https://github.com/tristanvdb/AutoCog/releases/latest)
![ctest](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-release-ctest.json)
![pytest](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-release-pytest.json)
![C++ coverage](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-release-cxx-coverage.json)
![C++ templates](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-release-cxx-templates.json)
![Python coverage](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-release-py-coverage.json)

[![candidate](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-candidate-status.json)](https://github.com/tristanvdb/AutoCog/actions/workflows/ci.yaml)
![ctest](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-candidate-ctest.json)
![pytest](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-candidate-pytest.json)
![C++ coverage](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-candidate-cxx-coverage.json)
![C++ templates](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-candidate-cxx-templates.json)
![Python coverage](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-candidate-py-coverage.json)

![License](https://img.shields.io/github/license/tristanvdb/AutoCog)
![Python](https://img.shields.io/badge/python-3.10%2B-blue)

**AutoCog** explores mechanisms to build automata that control applications driven by auto-regressive language models. We define a programming model called **Structured Thoughts**, with a language (STL) that compiles to executable automata.

## What is Structured Thoughts?

Structured Thoughts is a programming model where:
- **Prompts** are building blocks (like functions in traditional programming)
- **Automata** ensure completions can be parsed to extract structured data
- **Branching** between prompts is controlled by the language model
- **Dataflow** is statically defined and executed when instantiating automata

Example — a multiple-choice question with chain-of-thought:

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
  return { use answer; }
  annotate {
    _ as "You are answering a multiple choice questionnaire.";
    question as "the question that you have to answer";
    choices as "the four possible choices, only one is correct";
    work as "show your work step-by-step";
    answer as "you pick the index of the choice that best answers the question";
  }
}
```

Run it (use `--rng` to try it without a real model):

```bash
autocog run --stl mcq.stl --rng \
  --input '{"topic":"Science","question":"2+2?","choices":["3","4","5","6"]}'
```

The `autocog` CLI also compiles (`compile`) and serves programs (`serve`, `rpc`,
`backend`), and `autocog pack` bundles a program into a single distributable
`.stapp`. See the [documentation](docs/README.md) for the full CLI and the STL
language reference.

## How to get it

AutoCog builds a native llama.cpp backend, so the **preferred install is from
source** — it tunes the build to your CPU (and autodetects CUDA) for the best
inference performance. You need a C++17 toolchain and CMake; RE-flex and
llama.cpp are built automatically if not found on the system.

**From the source distribution (recommended)** — download `autocog-<version>.tar.gz`
from the [latest release](https://github.com/LLNL/AutoCog/releases/latest):

```bash
pip install autocog-<version>.tar.gz
```

**From a git checkout** (development / latest):

```bash
git clone --recursive https://github.com/LLNL/AutoCog
cd AutoCog
pip install .
```

**From the prebuilt wheel** — no toolchain needed, but **portable and untuned**
(generic CPU, no GPU); fine for a quick try. Grab the `.whl` for your Python from
the [release page](https://github.com/LLNL/AutoCog/releases/latest).

**Complete install** — add the `server` (FastAPI/uvicorn) and `validate`
(jsonschema) extras to any of the above, e.g. from a checkout:

```bash
pip install ".[server,validate]"
```

### Containers

Prebuilt server variants (`serve`, `rpc`, `backend`, `run`) for deployment:

```bash
docker build --target serve -f dockerfiles/ubuntu.df -t autocog:serve .
docker compose -f dockerfiles/compose.yaml up
```

See [dockerfiles/compose.yaml](dockerfiles/compose.yaml) for a multi-container example.

## License

AutoCog is distributed under the terms of the **Apache License (Version 2.0) with LLVM exceptions**.

All new contributions must be made under Apache-2.0 license (with LLVM exceptions).

SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

LLNL-CODE-850523
