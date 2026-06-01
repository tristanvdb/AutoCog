# Developer Guide

## Installation

### With pip

```bash
pip install .                # core
pip install ".[server]"      # with FastAPI/uvicorn for serve/rpc/backend
```

This builds everything via cmake internally (including vendored RE-flex and llama.cpp if not found on the system). Validate with:

```bash
python -c "import autocog; print(autocog.__version__)"
autocog compile --stl share/demos/mcq/select.stl | head -5
```

### With cmake (for C++ development)

You can either let cmake build vendored dependencies automatically:

```bash
mkdir -p builds/autocog && cd builds/autocog
cmake ../../AutoCog -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel $(nproc)
```

Or pre-build dependencies to a prefix for faster iteration:

```bash
PREFIX=$(cd .. && pwd)/opt

# RE-flex
cmake -B builds/reflex vendors/reflex -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build builds/reflex --parallel $(nproc) && cmake --install builds/reflex

# llama.cpp
cmake -B builds/llama vendors/llama \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -DLLAMA_BUILD_COMMON=OFF -DLLAMA_BUILD_TOOLS=OFF \
    -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_TESTS=OFF \
    -DLLAMA_BUILD_APP=OFF -DLLAMA_BUILD_SERVER=OFF \
    -DLLAMA_CUDA=OFF
cmake --build builds/llama --parallel $(nproc) && cmake --install builds/llama

# AutoCog (finds deps in $PREFIX)
cmake -B builds/autocog . -DCMAKE_PREFIX_PATH=$PREFIX -DCMAKE_BUILD_TYPE=Debug
cmake --build builds/autocog --parallel $(nproc)
```

For CUDA, replace `-DLLAMA_CUDA=OFF` with `-DLLAMA_CUDA=ON`.

Install ccache for faster rebuilds:

```bash
sudo apt-get install ccache   # or dnf install ccache
cmake ... -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
```

## Container Development

Build the builder stage for a dev container:

```bash
docker build --target builder -f dockerfiles/ubuntu.df -t autocog:dev .
```

For CUDA, pass a CUDA base image — `nvcc` is auto-detected:

```bash
docker build --target builder \
    --build-arg BUILD_IMAGE=nvidia/cuda:13.0.0-devel-ubuntu24.04 \
    -f dockerfiles/ubuntu.df -t autocog:cuda-dev .
```

Mount the source tree for iterative development:

```bash
docker run -it --rm -v $(pwd):/workspace -w /workspace autocog:dev bash
```

## Testing

See [tests/README.md](tests/README.md) for the full test structure,
fixture categories, golden output procedures, and coverage analysis.

### Quick Reference

```bash
# C++ tests (cmake build required)
cd builds/autocog
ctest -j4 --timeout 30

# Python tests (pip install required)
pytest tests/integration/modules tests/units/modules -v

# Coverage analysis (Debug + coverage → tests/coverage.md)
tests/scripts/tests-coverage.sh [BUILD_DIR] [VENV_DIR]
# Release-mode suite, or user-install smoke check:
tests/scripts/tests-release.sh [BUILD_DIR] [VENV_DIR]
tests/scripts/check-release.sh [BUILD_DIR] [VENV_DIR]
```

## Getting Models

For testing with real models (not required — the RNG model covers CI):

```bash
mkdir -p models && cd models
wget -O SmolLM3-Q4_K_M.gguf \
    https://huggingface.co/ggml-org/SmolLM3-3B-GGUF/resolve/main/SmolLM3-Q4_K_M.gguf?download=true
cd ..
```

## CLI Commands

| Command | Description |
|---------|-------------|
| `autocog compile` | Compile STL to STA JSON |
| `autocog run` | Run a program (from STL, STA, or .stapp) |
| `autocog pack` | Package STL into a .stapp |
| `autocog serve` | Full app server with web UI |
| `autocog rpc` | Prompt evaluation server |
| `autocog backend` | Inference server |
