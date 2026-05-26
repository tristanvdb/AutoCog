#!/usr/bin/env python3
"""
Integration test for C++ bindings.
Exercises all binding functions and the shared opaque ID store.
Run under ctest with PYTHONPATH set to binding build directories.
"""

import sys
import os
import json

import compiler_stl_cxx
import runtime_sta_cxx
import backend_llama_cxx

FIXTURES = sys.argv[1]  # path to tests/fixtures/stl/
SYNTAX   = sys.argv[2]  # path to share/syntax/default.json

errors = []

def check(name, condition, msg=""):
    if not condition:
        errors.append(f"FAIL: {name}: {msg}")
        print(f"  FAIL: {name}: {msg}")
    else:
        print(f"  OK: {name}")

# ============================================================================
# compiler_stl_cxx
# ============================================================================

print("compiler_stl_cxx:")

# compile
pid = compiler_stl_cxx.compile(f"{FIXTURES}/language/structures/test_basic_record.stl")
check("compile returns int", isinstance(pid, int))
check("compile returns positive id", pid > 0)

# compile with includes
pid2 = compiler_stl_cxx.compile(
    f"{FIXTURES}/language/symbols/test_import.stl",
    includes=[f"{FIXTURES}/language/symbols"]
)
check("compile with includes", pid2 > 0)

# emit
sta = compiler_stl_cxx.emit(pid, "sta")
check("emit returns dict", isinstance(sta, dict))
check("emit has prompts", "prompts" in sta)
check("emit has entry_points", "entry_points" in sta)

prompts = sta["prompts"]
check("emit has prompt 'test'", "test" in prompts)

prompt = prompts["test"]
check("prompt has fields", "fields" in prompt)
check("prompt has states", "states" in prompt)
check("prompt has sequence", "sequence" in prompt)
check("prompt has flows", "flows" in prompt)
check("prompt has channels", "channels" in prompt)

# emit with channels
pid3 = compiler_stl_cxx.compile(f"{FIXTURES}/flows/data/test_input_dataflow.stl")
sta3 = compiler_stl_cxx.emit(pid3, "sta")
channels = sta3["prompts"]["greeter"]["channels"]
check("input channel present", len(channels) > 0)
check("channel has type", channels[0]["type"] == "input")

# release
compiler_stl_cxx.release(pid)
compiler_stl_cxx.release(pid2)

try:
    compiler_stl_cxx.emit(pid, "sta")
    check("release invalidates id", False, "expected exception")
except RuntimeError:
    check("release invalidates id", True)

# ============================================================================
# runtime_sta_cxx
# ============================================================================

print("\nruntime_sta_cxx:")

# load_syntax
sid = runtime_sta_cxx.load_syntax(SYNTAX)
check("load_syntax returns int", isinstance(sid, int))
check("load_syntax returns positive id", sid > 0)

# instantiate
pid4 = compiler_stl_cxx.compile(f"{FIXTURES}/language/structures/test_basic_record.stl")
fta_id = runtime_sta_cxx.instantiate(pid4, "test", {}, sid)
check("instantiate returns int", isinstance(fta_id, int))
check("instantiate returns positive id", fta_id > 0)

# instantiate with content
pid5 = compiler_stl_cxx.compile(f"{FIXTURES}/flows/data/test_input_dataflow.stl")
fta_id2 = runtime_sta_cxx.instantiate(
    pid5, "greeter",
    {"name": "Alice"},
    sid
)
check("instantiate with content", fta_id2 > 0)

# instantiate nonexistent prompt
try:
    runtime_sta_cxx.instantiate(pid4, "nonexistent", {}, sid)
    check("instantiate bad prompt raises", False, "expected exception")
except RuntimeError:
    check("instantiate bad prompt raises", True)

# parse_text
text = "start:\nstatus:\n\tStatus:good\nnext: return\n"
result = runtime_sta_cxx.parse_text(pid4, "test", sid, text)
check("parse_text returns dict", isinstance(result, dict))
check("parse_text has next", "next" in result)
check("parse_text next value", result.get("next") == "return")

# release
runtime_sta_cxx.release_fta(fta_id)
runtime_sta_cxx.release_fta(fta_id2)
runtime_sta_cxx.release_syntax(sid)

# ============================================================================
# backend_llama_cxx
# ============================================================================

print("\nbackend_llama_cxx:")

# RNG model (always id 0)
check("rng vocab_size", backend_llama_cxx.vocab_size(0) == 258)

# tokenize
tokens = backend_llama_cxx.tokenize(0, "hello")
check("tokenize returns list", isinstance(tokens, list))
check("tokenize length", len(tokens) == 5)  # 'h','e','l','l','o' as bytes

# detokenize
text = backend_llama_cxx.detokenize(0, tokens)
check("detokenize roundtrip", text == "hello")

# evaluate — use select demo with pre-filled content
sid2 = runtime_sta_cxx.load_syntax(SYNTAX)
SHARE = os.path.dirname(os.path.dirname(SYNTAX))  # share/syntax/default.json → share/
pid6 = compiler_stl_cxx.compile(f"{SHARE}/demos/mcq/select.stl")
content = {"topic": "Sci", "question": "2+2?", "choices": ["3", "4", "5", "6"]}
fta_id3 = runtime_sta_cxx.instantiate(pid6, "main", content, sid2)

ftt_id = backend_llama_cxx.evaluate(0, fta_id3)
check("evaluate returns int", isinstance(ftt_id, int))

# get_best
paths = backend_llama_cxx.get_best(0, ftt_id, 1)
check("get_best returns list", isinstance(paths, list))
check("get_best has path", len(paths) == 1)
check("get_best path is string", isinstance(paths[0], str))
check("get_best path nonempty", len(paths[0]) > 0)

# release
backend_llama_cxx.release_ftt(ftt_id)
runtime_sta_cxx.release_fta(fta_id3)
runtime_sta_cxx.release_syntax(sid2)
compiler_stl_cxx.release(pid3)
compiler_stl_cxx.release(pid4)
compiler_stl_cxx.release(pid5)
compiler_stl_cxx.release(pid6)

# ============================================================================
# Summary
# ============================================================================

print()
if errors:
    print(f"FAILED: {len(errors)} errors")
    for e in errors:
        print(f"  {e}")
    sys.exit(1)
else:
    print("ALL PASSED")
    sys.exit(0)
