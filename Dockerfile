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

RUN python3 -m venv /venv
ENV PATH="/venv/bin:$PATH"

RUN pip install --upgrade pip setuptools wheel pybind11 numpy

COPY . /workspace/autocog
WORKDIR /workspace/autocog

RUN pip install -e .

