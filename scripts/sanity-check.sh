#!/bin/bash -ex

# Very basic

python3 -c "import autocog"

xfta --help
stlc -h

# Functional

## xFTA

python3 scripts/dump_sta_to_json.py tests/samples/mini.sta models/SmolLM3-Q4_K_M.gguf
xfta -m models/SmolLM3-Q4_K_M.gguf tests/samples/mini.sta.json

## autocog.llama.xfta

python3 scripts/execute_sta_with_llama_cpp.py tests/samples/mini.sta '{}' models/SmolLM3-Q4_K_M.gguf

## STLC

stlc tests/samples/defines.stl
stlc -I tests/samples/miniapp tests/samples/miniapp/main.stl
stlc tests/samples/miniapp/more.stl

