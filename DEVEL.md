# Develop Commands Cheat Sheet

## Get models

```
mkdir -p  models
cd models
wget -O SmolLM3-Q4_K_M.gguf https://huggingface.co/ggml-org/SmolLM3-3B-GGUF/resolve/main/SmolLM3-Q4_K_M.gguf?download=true
```

## Container

### CPU (for dev)

Build the image:
```
docker build -t autocog:ubi -f Dockerfile.ubi .
```

Run one-off commands:
```
docker run --rm -v $(pwd):/workspace -w /workspace autocog:ubi scripts/sanity-check.sh
```

**Recommended: Use a persistent container for development:**
```bash
# Start persistent container
docker run -d --name autocog --rm -v $(pwd):/workspace -w /workspace autocog:ubi sleep infinity

# Execute commands in the container
docker exec autocog bash -c "cd /tmp && cmake /workspace && make install && ctest"

# Interactive shell
docker exec -it autocog bash

# Stop container when done
docker stop autocog
```

### CUDA on RHEL with Podman

First setup CUDA for container use (3rd command is for FIPS enable machines):
```bash
curl -s -L https://nvidia.github.io/libnvidia-container/stable/rpm/nvidia-container-toolkit.repo | \
    sudo tee /etc/yum.repos.d/nvidia-container-toolkit.repo
sudo dnf --disablerepo=\* --enablerepo=nvidia-container-toolkit-experimental install -y nvidia-container-toolkit
sudo rpm -ivh --nodigest --nofiledigest /var/cache/dnf/nvidia-container-toolkit-experimental-*/packages/*.rpm
sudo nvidia-ctk cdi generate --output=/etc/cdi/nvidia.yaml
sudo chmod o+r /etc/cdi/nvidia.yaml
sudo chmod o+rx /etc/cdi
podman run --rm --device nvidia.com/gpu=all docker.io/nvidia/cuda:12.4.0-runtime-ubuntu22.04 nvidia-smi
```

Build and run persistent container:
```bash
podman build --device nvidia.com/gpu=all -f Dockerfile.ubi-cuda -t autocog:ubi-cuda .
podman run -d --name autocog --rm --device nvidia.com/gpu=all \
    -v $(pwd):/workspace -w /workspace autocog:ubi-cuda sleep infinity

# Execute commands
podman exec autocog bash -c "cd /tmp && cmake /workspace && make install && ctest"
```

### Ubuntu with CUDA (Docker)

```bash
docker build -t autocog:ubuntu -f Dockerfile.ubuntu .
docker run -d --name autocog --rm --gpus all \
    -v $(pwd):/workspace -w /workspace autocog:ubuntu sleep infinity
```

## Testing Components

### xFTA (Finite Thoughts Automaton Executor)

Testing the C++ utility:
```bash
python3 scripts/dump_sta_to_json.py tests/samples/mini.sta models/SmolLM3-Q4_K_M.gguf
xfta -v -m models/SmolLM3-Q4_K_M.gguf tests/samples/mini.sta.json
```

Testing the integration:
```bash
python3 scripts/execute_sta_with_llama_cpp.py tests/samples/mini.sta '{}' models/SmolLM3-Q4_K_M.gguf
```

### STLC (Structured Thoughts Language Compiler)

Test compilation:
```bash
stlc -h
stlc tests/samples/defines.stl
stlc share/demos/story-writer/story-writer.stl
```

## Building and Testing

### Build Types

**Debug build** (with exception backtrace wrapper):
```bash
mkdir -p /tmp/autocog && cd /tmp/autocog
cmake /workspace -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/opt
make install -j$(nproc)
```

**Release build** (optimized, default):
```bash
mkdir -p /tmp/autocog && cd /tmp/autocog
cmake /workspace -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt
make install -j$(nproc)
```

### Running Tests

Run all tests:
```bash
cd /tmp/autocog
ctest --output-on-failure
```

Run specific test suites:
```bash
ctest -R stl_parser          # All parser tests
ctest -R smoke               # Smoke tests only
ctest -R stl_parser_identifiers -V  # Single test with verbose output
```

Run in persistent container:
```bash
docker exec autocog bash -c "cd /tmp && cmake /workspace -DCMAKE_BUILD_TYPE=Release && make install -j\$(nproc) && ctest --output-on-failure"
```

### Python Package Testing

Install and test the Python package:
```bash
pip install /workspace
python -c "import autocog"
python -c "import autocog.compiler.stl"
python -c "import autocog.llama.xfta"
```

