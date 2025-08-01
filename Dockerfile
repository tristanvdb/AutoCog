FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PYTHONUNBUFFERED=1

RUN apt-get update && \
    apt-get install -y \
        build-essential \
        cmake \
        pkg-config \
        python3 \
        python3-pip \
        python3-dev \
        python3-venv && \
    rm -rf /var/lib/apt/lists/*

# LLama.cpp

COPY vendors/llama /tmp/llama_cpp

RUN cd /tmp/llama_cpp && \
      cmake -DLLAMA_BUILD_COMMON=OFF -B build && \
      make -C build -j$(nproc) install

ENV LD_LIBRARY_PATH="/usr/local/lib:$LD_LIBRARY_PATH"

# Venv

RUN python3 -m venv /opt
ENV PATH="/opt/bin:$PATH"
ENV PYTHONPATH="/opt/bin:$PATH"
RUN pip install --upgrade pip

# Autocog

COPY . /workspace/autocog

RUN rm -rf /workspace/autocog/vendors && \
    pip install /workspace/autocog && \
    rm -rf /workspace/autocog

WORKDIR /workspace

