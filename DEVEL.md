# Develop Commands Cheat Sheet

## C++ module: `autocog.llama`

Build image and "test":
```
docker build -t autocog:latest .
docker run --rm -v $(pwd):/workspace/autocog -it autocog:latest python3 /workspace/autocog/tests/autocog/llama/execute_sta_with_llama_cpp.py /workspace/autocog/tests/samples/mini.sta '{}' /workspace/autocog/models/SmolLM3-Q4_K_M.gguf
```

In container:
```
docker run --rm -v $(pwd):/workspace/autocog -w /workspace/autocog -it autocog:latest bash
apt update && apt install -y gdb vim
pip install -e .
python3 tests/autocog/llama/execute_sta_with_llama_cpp.py tests/samples/mini.sta '{}' models/SmolLM3-Q4_K_M.gguf
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
podman exec -ti autocog python3 tests/autocog/llama/execute_sta_with_llama_cpp.py tests/samples/mini.sta '{}' models/SmolLM3-Q4_K_M.gguf
```

## C++ module: `autocog.compiler.stl`

```
docker run --rm -v $(pwd):/workspace/autocog -w /workspace/autocog -it autocog:latest bash
mkdir -p /workspace/libs/autocog/compiler/stl/__build/
( cd /workspace/libs/autocog/compiler/stl/__build/ && rm -rf * && cmake .. && make install )
```
