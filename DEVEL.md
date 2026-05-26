# Develop Commands Cheat Sheet

## Get models

```
mkdir -p  models
cd models
wget -O SmolLM3-Q4_K_M.gguf https://huggingface.co/ggml-org/SmolLM3-3B-GGUF/resolve/main/SmolLM3-Q4_K_M.gguf?download=true
```

## Container

### CPU (for dev)

```
docker build -t autocog:ubi -f Dockerfile.ubi .
podman run --rm -v $(pwd):/workspace -w /workspace -ti autocog:ubi scripts/sanity-check.sh
docker run --rm --name autocog -v $(pwd):/workspace/autocog -w /workspace/autocog -it autocog:ubi sleep infinity
```

### CUDA on RHEL with Podman

First setup CUDA for container use (3rd command is for FIPS enable machines):
```
curl -s -L https://nvidia.github.io/libnvidia-container/stable/rpm/nvidia-container-toolkit.repo |     sudo tee /etc/yum.repos.d/nvidia-container-toolkit.repo
sudo dnf --disablerepo=\* --enablerepo=nvidia-container-toolkit-experimental install -y nvidia-container-toolkit
sudo rpm -ivh --nodigest --nofiledigest /var/cache/dnf/nvidia-container-toolkit-experimental-*/packages/*.rpm
sudo nvidia-ctk cdi generate --output=/etc/cdi/nvidia.yaml
sudo chmod o+r /etc/cdi/nvidia.yaml
sudo chmod o+rx /etc/cdi
podman run --rm --device nvidia.com/gpu=all docker.io/nvidia/cuda:12.4.0-runtime-ubuntu22.04 nvidia-smi
```

Then building and running the container:
```
podman build --device nvidia.com/gpu=all -f Dockerfile.ubi-cuda -t autocog:ubi-cuda .\
podman run --name autocog --rm -d --device nvidia.com/gpu=all -v $(pwd):/workspace/autocog -w /workspace/autocog -it autocog:ubi-cuda sleep infinity
```

## xFTA

Testing the C++ utility
```
python3 scripts/dump_sta_to_json.py tests/samples/mini.sta models/SmolLM3-Q4_K_M.gguf
xfta -v -m models/SmolLM3-Q4_K_M.gguf tests/samples/mini.sta.json
```

Testing the integration:
```
python3 scripts/execute_sta_with_llama_cpp.py /workspace/autocog/tests/samples/mini.sta '{}' /workspace/autocog/models/SmolLM3-Q4_K_M.gguf
```

## STLC

```
mkdir -p /tmp/autocog
( cd /tmp/autocog && rm -rf * && cmake /workspace/autocog && make install )
( cd /workspace/tests/samples ; autocog-compiler-stl -I miniapp miniapp/main.stl miniapp/more.stl defines.stl )
```

## Faster CMake build for develop

TODO: what to add in container? (pip uses an ephemeral venv)

Building both executables:
```
mkdir -p /tmp/autocog
cd /tmp/autocog
cmake /workspace/autocog
make install -j$(nproc)
xfta --help
stlc --help
```

