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
SEARCH   = sys.argv[3]  # path to share/search/default.json

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
except Exception:
    check("release invalidates id", True)

# ============================================================================
# runtime_sta_cxx
# ============================================================================

print("\nruntime_sta_cxx:")

# load_syntax
sid = runtime_sta_cxx.load_syntax(SYNTAX)
check("load_syntax returns int", isinstance(sid, int))
check("load_syntax returns positive id", sid > 0)

# load_search_config
scid = runtime_sta_cxx.load_search_config(SEARCH)
check("load_search_config returns int", isinstance(scid, int))
check("load_search_config returns positive id", scid > 0)

# instantiate
pid4 = compiler_stl_cxx.compile(f"{FIXTURES}/language/structures/test_basic_record.stl")
fta_id = runtime_sta_cxx.instantiate(pid4, "test", {}, sid, scid)
check("instantiate returns int", isinstance(fta_id, int))
check("instantiate returns positive id", fta_id > 0)

# instantiate with content
pid5 = compiler_stl_cxx.compile(f"{FIXTURES}/flows/data/test_input_dataflow.stl")
fta_id2 = runtime_sta_cxx.instantiate(
    pid5, "greeter",
    {"name": "Alice"},
    sid, scid
)
check("instantiate with content", fta_id2 > 0)

# instantiate nonexistent prompt
try:
    runtime_sta_cxx.instantiate(pid4, "nonexistent", {}, sid, scid)
    check("instantiate bad prompt raises", False, "expected exception")
except Exception:
    check("instantiate bad prompt raises", True)

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
fta_id3 = runtime_sta_cxx.instantiate(pid6, "main", content, sid2, scid)

ftt_id = backend_llama_cxx.evaluate(0, fta_id3)
check("evaluate returns int", isinstance(ftt_id, int))

# get_ftt_json
import json
ftt_str = backend_llama_cxx.get_ftt_json(0, fta_id3, ftt_id)
check("get_ftt_json returns string", isinstance(ftt_str, str))
ftt = json.loads(ftt_str)
check("ftt has uid", "uid" in ftt)
check("ftt has text", "text" in ftt)
check("ftt has children", "children" in ftt)

# release
backend_llama_cxx.release_ftt(ftt_id)
runtime_sta_cxx.release_fta(fta_id3)
runtime_sta_cxx.release_syntax(sid2)

# ============================================================================
# Search variants: exercise repetition + diversity code paths
# ============================================================================

SEARCH_FULL = os.path.join(SHARE, "search", "full.json")
scid_full = runtime_sta_cxx.load_search_config(SEARCH_FULL)
check("full search config loaded", scid_full > 0)

sid3 = runtime_sta_cxx.load_syntax(SYNTAX)
fta_id4 = runtime_sta_cxx.instantiate(pid6, "main", content, sid3, scid_full)
ftt_id2 = backend_llama_cxx.evaluate(0, fta_id4)
check("evaluate with full search", isinstance(ftt_id2, int))

ftt_str2 = backend_llama_cxx.get_ftt_json(0, fta_id4, ftt_id2)
check("ftt_json with full search", len(ftt_str2) > 0)

backend_llama_cxx.release_ftt(ftt_id2)
runtime_sta_cxx.release_fta(fta_id4)
runtime_sta_cxx.release_syntax(sid3)

# ============================================================================
# store_fta_json + load_program
# ============================================================================

# store_fta_json: round-trip FTA through store
sid4 = runtime_sta_cxx.load_syntax(SYNTAX)
fta_id5 = runtime_sta_cxx.instantiate(pid6, "main", content, sid4, scid)
fta_json_str = runtime_sta_cxx.get_fta_json(fta_id5)
stored_id = runtime_sta_cxx.store_fta_json(fta_json_str)
check("store_fta_json returns id", stored_id > 0)
roundtrip = runtime_sta_cxx.get_fta_json(stored_id)
check("store_fta_json roundtrip", roundtrip == fta_json_str)
runtime_sta_cxx.release_fta(stored_id)
runtime_sta_cxx.release_fta(fta_id5)
runtime_sta_cxx.release_syntax(sid4)

# cleanup
compiler_stl_cxx.release(pid3)
compiler_stl_cxx.release(pid4)
compiler_stl_cxx.release(pid5)
compiler_stl_cxx.release(pid6)

# ============================================================================
# Logging: bridge sink + set_log_level
# ============================================================================

import logging

# set_log_level should be callable
runtime_sta_cxx.set_log_level(10)  # DEBUG
check("set_log_level DEBUG", True)

# Capture records via Python logging
records = []
handler = logging.Handler()
handler.emit = lambda r: records.append(r)
logger = logging.getLogger("autocog")
logger.addHandler(handler)
logger.setLevel(1)  # capture everything

# Set C++ to INFO and compile something — should produce records
runtime_sta_cxx.set_log_level(20)  # INFO

pid_log = compiler_stl_cxx.compile(os.path.join(FIXTURES, "language/structures/test_basic_record.stl"))
info_records = [r for r in records if r.levelno >= 20]
check("bridge sink receives INFO", len(info_records) > 0,
      f"got {len(info_records)} records")

# Set to ERROR — should filter out INFO/DEBUG
records.clear()
runtime_sta_cxx.set_log_level(40)  # ERROR

pid_log2 = compiler_stl_cxx.compile(os.path.join(FIXTURES, "language/structures/test_basic_record.stl"))
below_error = [r for r in records if r.levelno < 40]
check("bridge sink filters below ERROR", len(below_error) == 0,
      f"got {len(below_error)} records below ERROR")

logger.removeHandler(handler)
runtime_sta_cxx.set_log_level(30)  # restore WARNING
compiler_stl_cxx.release(pid_log)
compiler_stl_cxx.release(pid_log2)

# ============================================================================
# Error translator: typed exceptions across binding boundary
# ============================================================================

# ConfigError from bad syntax file
try:
    runtime_sta_cxx.load_syntax("/tmp/nonexistent_syntax_binding_test.json")
    check("ConfigError from bad syntax", False, "no exception raised")
except Exception as e:
    type_name = type(e).__name__
    has_path = hasattr(e, 'path')
    check("ConfigError from bad syntax",
          type_name == "ConfigError" and has_path,
          f"type={type_name} has_path={has_path}")

# ConfigError from bad search config
try:
    runtime_sta_cxx.load_search_config("/tmp/nonexistent_search_binding_test.json")
    check("ConfigError from bad search", False, "no exception raised")
except Exception as e:
    check("ConfigError from bad search", type(e).__name__ == "ConfigError",
          f"type={type(e).__name__}")

# FileError from bad program file
try:
    runtime_sta_cxx.load_program("/tmp/nonexistent_program_binding_test.json")
    check("FileError from bad program", False, "no exception raised")
except Exception as e:
    type_name = type(e).__name__
    is_oserror = isinstance(e, OSError)
    check("FileError from bad program",
          type_name == "FileError" and is_oserror,
          f"type={type_name} is_oserror={is_oserror}")

# Compile errors surface as diagnostics, not exceptions. The raw binding
# returns a program id and records error-level diagnostics retrievable by id;
# the Python layer is what turns those into a raised CompileError.
bad_pid = compiler_stl_cxx.compile(f"{FIXTURES}/errors/test_missing_semicolon.stl")
bad_diags = compiler_stl_cxx.get_diagnostics(bad_pid)
has_error = any(d.get("level") == "error" for d in bad_diags)
check("bad STL records error diagnostics", has_error,
      f"num_diags={len(bad_diags)}")
# Each diagnostic carries a resolved file path and line/column.
located = [d for d in bad_diags if d.get("location")]
check("diagnostic carries resolved location", bool(located),
      f"located={len(located)}/{len(bad_diags)}")
if located:
    loc = located[0]["location"]
    check("diagnostic location has file/line/column",
          loc.get("file", "").endswith(".stl")
          and isinstance(loc.get("line"), int)
          and isinstance(loc.get("column"), int),
          f"loc={loc}")
compiler_stl_cxx.release(bad_pid)

# A successful compile records no error-level diagnostics.
ok_pid = compiler_stl_cxx.compile(f"{FIXTURES}/language/structures/test_basic_record.stl")
ok_diags = compiler_stl_cxx.get_diagnostics(ok_pid)
check("clean STL has no error diagnostics",
      not any(d.get("level") == "error" for d in ok_diags),
      f"num_diags={len(ok_diags)}")
compiler_stl_cxx.release(ok_pid)

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
