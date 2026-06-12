# Developer Guide

## Test-driven workflow (start here)

The developer loop *is* the test harness. One command builds AutoCog
(Debug + coverage) into a venv and runs the full C++ (`ctest`) and Python
(`pytest`) suites, writing a coverage report to `tests/coverage.md`:

```bash
tests/scripts/tests-coverage.sh build/ venv/
```

`build/` and `venv/` are reused across runs (kept warm via `ccache`); omit them
to use throwaway temp dirs. This is the command to run while developing.

The suite exercises a real model, not just the built-in RNG: the session
fixtures auto-download a tiny test GGUF (`tiny-llama3-test-Q2_K.gguf`, ~21 MB) to
`models/` on first use. For offline/air-gapped work, set
`AUTOCOG_NO_MINIMAL_GGUF=1` to skip the real-model tests, or pre-fetch it:

```bash
mkdir -p models && cd models
wget https://huggingface.co/TensorBlock/tiny-llama3-test-GGUF/resolve/main/tiny-llama3-test-Q2_K.gguf
```

See [tests/README.md](tests/README.md) for the test structure, fixtures,
golden-output procedure, the branching model, and the CI/release pipeline (the
`*-release` / `check-*` scripts are CI/CD concerns and live there).

## Building directly

You rarely build by hand — the test scripts (and `pip install .`) drive CMake
for you. Build directly only when working on the C++ and you want to confirm it
compiles; a direct CMake build configures the test suite by default:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel $(nproc)
ctest --test-dir build -j4
```

RE-flex and llama.cpp are built from the vendored submodules if not found on the
system. CUDA is autodetected (`find_package(CUDAToolkit)`); force it with
`-DAUTOCOG_CUDA=ON|OFF`. For faster iteration, install `ccache` and/or prebuild
the vendored deps to a prefix and point `-DCMAKE_PREFIX_PATH` at it (see the
option block in `CMakeLists.txt`). To install into your own environment, use the
README's install instructions (e.g. `pip install ".[server,validate]"`).

## Containers

A dev container with the full toolchain:

```bash
docker build --target builder -f dockerfiles/ubuntu.df -t autocog:dev .
docker run -it --rm -v $(pwd):/workspace -w /workspace autocog:dev bash
```

For CUDA, pass a CUDA base image —
`--build-arg BUILD_IMAGE=nvidia/cuda:13.0.0-devel-ubuntu24.04`.
