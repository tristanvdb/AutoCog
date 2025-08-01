&#9881; Automaton & Cognition
=============================

|   | PIP | Frontend | CLI |
|---|---|---|---|
| `master` | [![PIP](https://github.com/LLNL/AutoCog/workflows/pip/badge.svg?branch=master)](https://github.com/LLNL/AutoCog/actions) | [![Frontend](https://github.com/LLNL/AutoCog/workflows/frontend/badge.svg?branch=master)](https://github.com/LLNL/AutoCog/actions) | [![CLI](https://github.com/LLNL/AutoCog/actions/workflows/cli.yml/badge.svg?branch=master)](https://github.com/LLNL/AutoCog/actions) |
| `devel` | [![PIP](https://github.com/LLNL/AutoCog/workflows/pip/badge.svg?branch=devel)](https://github.com/LLNL/AutoCog/actions) | [![Frontend](https://github.com/LLNL/AutoCog/workflows/frontend/badge.svg?branch=devel)](https://github.com/LLNL/AutoCog/actions) | [![CLI](https://github.com/LLNL/AutoCog/actions/workflows/cli.yml/badge.svg?branch=devel)](https://github.com/LLNL/AutoCog/actions) |

Automaton & Cognition explores mechanisms to build automaton that control applications driven by auto-regressive language models.
To this end, we defined a programming model, Structured Thoughts, with a language that compiles to a set of automaton.

We broke down the documentation into a few files:
 - [setup](./docs/setup.md)
 - [usage](./docs/usage.md)
 - [language](./docs/language.md)
 - [tutorial](./docs/tutorial.md)
 - [roadmap](./docs/roadmap.md)

The libraries have [their own documentation](./share/library/README.md).

## Develop Command Cheat Sheet

### C++ module: autocog.llama

Build image and "test":
```
docker build -t autocog:latest .
docker run --rm -it autocog:latest python3 -c "import autocog.llama; print(autocog.llama.__doc__)"
docker run --rm -v $(pwd):/workspace/autocog -it autocog:latest python3 /workspace/autocog/tests/autocog/llama/roundtrip_tokenization.py /workspace/autocog/models/stories260K.gguf
docker run --rm -v $(pwd):/workspace/autocog -it autocog:latest python3 /workspace/autocog/tests/autocog/compilation/compile_sta_to_ftas.py /workspace/autocog/share/library/mcq/select.sta '{"topic":"a topic", "question":"my question", "choices" : [ "a", "b", "c", "d" ] }'
docker run --rm -v $(pwd):/workspace/autocog -it autocog:latest python3 /workspace/autocog/tests/autocog/llama/execute_sta_with_llama_cpp.py /workspace/autocog/share/library/mcq/select.sta '{"topic":"a topic", "question":"my question", "choices" : [ "a", "b", "c", "d" ] }' /workspace/autocog/models/stories260K.gguf
```

Fast rebuild (in mounted:
```
docker run --rm -v $(pwd):/workspace/autocog -w /workspace/autocog -it autocog:latest python setup.py build_ext --inplace
```

## Contributing

Contributions are welcome!

So far there is only one rule: **linear git history** (no merge commits).
Only the master branch have stable commits, other branches might be rebased without notice.

Version number should increase for each push to `master` and have a matching tag.

## License

AutoCog is distributed under the terms of the Apache License (Version 2.0) with LLVM exceptions.

All new contributions must be made under Apache-2.0 license (with LLVM exceptions).

SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

LLNL-CODE-850523
