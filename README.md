# ⚙️ AutoCog - Automaton & Cognition

[![CI](https://github.com/tristanvdb/AutoCog/actions/workflows/ci.yaml/badge.svg?branch=master)](https://github.com/tristanvdb/AutoCog/actions/workflows/ci.yaml)
[![Release](https://github.com/tristanvdb/AutoCog/actions/workflows/release.yaml/badge.svg)](https://github.com/tristanvdb/AutoCog/actions/workflows/release.yaml)
![ctest](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-ctest.json)
![pytest](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-pytest.json)
![C++ coverage](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-cxx-coverage.json)
![Python coverage](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/tristanvdb/2b5d528fd69eb7528731a4ca2b5ca3ae/raw/autocog-py-coverage.json)

**AutoCog** explores mechanisms to build automata that control applications driven by auto-regressive language models. We define a programming model called **Structured Thoughts**, with a language (STL) that compiles to executable automata.

## Quick Start

```bash
git clone --recursive https://github.com/LLNL/AutoCog
cd AutoCog
```

Install with pip:

```bash
pip install .
```

Or build a container:

```bash
podman build -f dockerfiles/ubi.df -t autocog:ubi .
```

See [DEVEL.md](./DEVEL.md) for development setup and [docs/](./docs/README.md) for documentation.

## What is Structured Thoughts?

Structured Thoughts is a programming model where:
- **Prompts** are building blocks (like functions in traditional programming)
- **Automata** ensure completions can be parsed to extract structured data
- **Branching** between prompts is controlled by the language model
- **Dataflow** is statically defined and executed when instantiating automata

Example - Multiple Choice Question with Chain of Thought:

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

## Examples

The [share/demos/mcq/](./share/demos/mcq/) directory contains 10 Multiple Choice Question examples demonstrating various thought patterns:

- **Basic**: Simple select/repeat
- **Chain of Thought**: Step-by-step reasoning
- **Hypothesis**: Two-stage generation
- **Iterative**: Retry loops with reflection

See [share/demos/mcq/README.md](./share/demos/mcq/README.md) for details.

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
