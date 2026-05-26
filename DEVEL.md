# Developer Guide

## Container Development

### Building the Image

Build the builder stage which includes all vendored dependencies and build tools:

```bash
podman build --target builder -f dockerfiles/ubi.df -t autocog:ubi-dev .
```

For CUDA, pass a CUDA base image — `nvcc` is auto-detected:

```bash
podman build --target builder \
    --build-arg BUILD_IMAGE=docker.io/nvidia/cuda:13.0.0-devel-ubi9 \
    -f dockerfiles/ubi.df -t autocog:ubi-cuda-dev .
```

Ubuntu variant:

```bash
podman build --target builder -f dockerfiles/ubuntu.df -t autocog:ubuntu-dev .
```

### Persistent Dev Container

Mount the source tree and use the container for builds:

```bash
podman run -d --name autocog-dev --rm \
    -v $(pwd):/workspace/autocog -w /workspace/autocog \
    autocog:ubi-dev sleep infinity
```

Interactive shell:

```bash
podman exec -it autocog-dev bash
```

Stop when done:

```bash
podman stop autocog-dev
```

## Host-side Development

Prerequisites:

- C++17 compiler (GCC 9+ or Clang 10+)
- CMake 3.18+
- Python 3.9+ with development headers
- pybind11 (`pip install pybind11`)

Build the vendored dependencies to a local prefix. Pick a location (examples use `../opt`):

```bash
PREFIX=$(cd .. && pwd)/opt
mkdir -p ../builds/reflex ../builds/llama
```

### Build Vendored Dependencies

#### RE-flex

```bash
cd ../builds/reflex
cmake ../../AutoCog/vendors/reflex -DCMAKE_INSTALL_PREFIX=$PREFIX
cmake --build . --parallel $(nproc)
cmake --install .
cd ../../AutoCog
```

#### llama.cpp

```bash
cd ../builds/llama
cmake ../../AutoCog/vendors/llama \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -DLLAMA_BUILD_COMMON=OFF \
    -DLLAMA_BUILD_TOOLS=OFF \
    -DLLAMA_BUILD_EXAMPLES=OFF \
    -DLLAMA_BUILD_TESTS=OFF \
    -DLLAMA_BUILD_APP=OFF \
    -DLLAMA_BUILD_SERVER=OFF \
    -DLLAMA_CUDA=OFF
cmake --build . --parallel $(nproc)
cmake --install .
cd ../../AutoCog
```

For CUDA, replace `-DLLAMA_CUDA=OFF` with `-DLLAMA_CUDA=ON` (requires CUDA toolkit).

## Building AutoCog

### With CMake

From a container:

```bash
mkdir -p /tmp/autocog && cd /tmp/autocog
cmake /workspace/autocog -DCMAKE_INSTALL_PREFIX=/opt
make -j$(nproc)
```

From the host:

```bash
mkdir -p ../builds/autocog && cd ../builds/autocog
cmake ../../AutoCog \
    -DCMAKE_PREFIX_PATH=$PREFIX \
    -DCMAKE_INSTALL_PREFIX=$PREFIX
make -j$(nproc)
```

Debug build (with exception backtrace wrapper):

```bash
cmake ... -DCMAKE_BUILD_TYPE=Debug
```

This builds `stlc`, `xfta`, the Python bindings (`.so`), and the test harness.

### With pip

```bash
pip install .
```

This runs the CMake build internally and installs the Python package, bindings, and CLI tools. Validate with:

```bash
scripts/test-pip-install.sh
```

## Testing

### ctest Labels

```bash
ctest --output-on-failure          # All tests
ctest -L smoke                     # CLI smoke tests
ctest -L stlc                     # Compiler: parser units + integration stages
ctest -L xfta                     # Executor: RNG model fixtures + binding
ctest -L bindings                  # Python binding .so import tests
ctest -L demo                     # Demo program compilation
```

Run a single test with verbose output:

```bash
ctest -R stl_parser_identifiers -V
```

### Testing stlc

```bash
stlc -h
stlc share/demos/mcq/select.stl
stlc -I share/library share/demos/story-writer/writer.stl
stlc --stage parse share/demos/mcq/select.stl
```

### Testing xfta

With the built-in RNG model (no GGUF needed):

```bash
xfta --rng tests/integration/xfta/test_text_completion.json
xfta --rng -v tests/integration/xfta/test_text_choice.json
```

With a real model:

```bash
xfta -v -m models/SmolLM3-Q4_K_M.gguf fta_input.json
```

### Python Package

After `pip install .`:

```bash
scripts/test-pip-install.sh
```

Or manually:

```bash
python3 -c "import autocog"
python3 -c "import autocog.compiler.stl"
python3 -c "import autocog.llama.xfta"
```

## Getting Models

For testing with real models (not required — the RNG model covers CI):

```bash
mkdir -p models && cd models
wget -O SmolLM3-Q4_K_M.gguf \
    https://huggingface.co/ggml-org/SmolLM3-3B-GGUF/resolve/main/SmolLM3-Q4_K_M.gguf?download=true
cd ..
```
