#!/usr/bin/env python3
"""
Integration test for C++ bindings.
Exercises the uniform five-verb surface (get/load/read/store/dump) for every
tracked structure (sta, fta, ftt, syntax, search), opaque string handles, and
the shared datastore. Run under ctest with PYTHONPATH set to binding build dirs.
"""

import sys
import os
import json
import tempfile

import compiler_stl_cxx
import runtime_sta_cxx
import backend_llama_cxx

FIXTURES = sys.argv[1]
SYNTAX   = sys.argv[2]
SEARCH   = sys.argv[3]
SHARE    = os.path.dirname(os.path.dirname(SYNTAX))

errors = []

def check(name, condition, msg=""):
    if not condition:
        errors.append(f"FAIL: {name}: {msg}")
        print(f"  FAIL: {name}: {msg}")
    else:
        print(f"  OK: {name}")

def tmp(name):
    return os.path.join(tempfile.gettempdir(), name)

# ===== compiler_stl_cxx — compile returns an opaque string handle =====
print("compiler_stl_cxx:")
pid = compiler_stl_cxx.compile(f"{FIXTURES}/language/structures/test_basic_record.stl")
check("compile returns str handle", isinstance(pid, str))
pid2 = compiler_stl_cxx.compile(
    f"{FIXTURES}/language/symbols/test_import.stl",
    includes=[f"{FIXTURES}/language/symbols"])
check("compile with includes", isinstance(pid2, str))
check("get_diagnostics returns list", isinstance(compiler_stl_cxx.get_diagnostics(pid), list))
compiler_stl_cxx.release(pid2)

# ===== sta — full five-verb surface =====
print("\nsta:")
sta = runtime_sta_cxx.get_sta(pid)
check("get_sta returns dict", isinstance(sta, dict))
check("get_sta has prompts", "prompts" in sta)
check("get_sta has entry_points", "entry_points" in sta)
check("get_sta prompt 'test'", "test" in sta["prompts"])
dumped = runtime_sta_cxx.dump_sta(pid)
check("dump_sta returns str", isinstance(dumped, str))
rid = runtime_sta_cxx.read_sta(dumped)
check("read_sta returns str handle", isinstance(rid, str))
check("read_sta round-trip identical", runtime_sta_cxx.dump_sta(rid) == dumped)
check("read_sta dedup: identical content -> same handle", rid == pid)
rid_d = runtime_sta_cxx.read_sta(sta)
check("read_sta (dict) round-trip identical", runtime_sta_cxx.dump_sta(rid_d) == dumped)
check("read_sta (dict) dedup", rid_d == pid)
sta_path = tmp("binding_test_sta.json")
runtime_sta_cxx.store_sta(pid, sta_path)
check("store_sta wrote file", os.path.isfile(sta_path))
lid = runtime_sta_cxx.load_sta(sta_path)
check("load_sta round-trip identical", runtime_sta_cxx.dump_sta(lid) == dumped)
check("load_sta dedup", lid == pid)
os.remove(sta_path)
# rid / rid_d / lid each took a holder on the shared (content-addressed) entry;
# releasing them drops those holders. pid keeps its own holder until the end.
runtime_sta_cxx.release_sta(rid)
runtime_sta_cxx.release_sta(rid_d)
runtime_sta_cxx.release_sta(lid)

# ===== vocab tables exercise the shared VocabExpr Python codec =====
# select.stl/basic_record have no vocab table, so the VocabExpr to_py/from_py
# paths only run for a program that declares one. Round-trip such an STA through
# the Python door (get_sta -> to_py, read_sta(dict) -> from_py).
print("\nvocab (sta with vocab table):")
vpid = compiler_stl_cxx.compile(f"{FIXTURES}/language/vocab/test_vocab.stl")
vsta = runtime_sta_cxx.get_sta(vpid)
check("vocab sta carries a vocabs table",
      any(p.get("vocabs") for p in vsta["prompts"].values()))
vrid = runtime_sta_cxx.read_sta(vsta)
check("vocab sta dict round-trips through the Python door",
      runtime_sta_cxx.dump_sta(vrid) == runtime_sta_cxx.dump_sta(vpid))
runtime_sta_cxx.release_sta(vrid)
runtime_sta_cxx.release_sta(vpid)

# ===== call channels + clauses exercise the Channel Python codec =====
# The story-writer demo is call-channel and clause heavy, covering the
# CallChannel / DataflowChannel / InputChannel variants and the bind/ravel/wrap/
# prune/mapped clauses in channel-python (which select.stl/basic_record do not).
print("\nchannels (sta with call channels + clauses):")
wpid = compiler_stl_cxx.compile(
    f"{SHARE}/demos/story-writer/writer.stl",
    includes=[f"{SHARE}/library/stlib", f"{SHARE}/demos/story-writer"])
wsta = runtime_sta_cxx.get_sta(wpid)
check("writer sta has channels",
      any(p.get("channels") for p in wsta["prompts"].values()))
wrid = runtime_sta_cxx.read_sta(wsta)
check("writer sta dict round-trips through the Python door",
      runtime_sta_cxx.dump_sta(wrid) == runtime_sta_cxx.dump_sta(wpid))
runtime_sta_cxx.release_sta(wrid)
runtime_sta_cxx.release_sta(wpid)

# ===== syntax / search — full five-verb surface =====
print("\nsyntax / search:")
sid = runtime_sta_cxx.load_syntax(SYNTAX)
check("load_syntax returns str handle", isinstance(sid, str))
syn = runtime_sta_cxx.get_syntax(sid)
check("get_syntax returns dict", isinstance(syn, dict))
check("get_syntax has system_msg", "system_msg" in syn)
syn_dump = runtime_sta_cxx.dump_syntax(sid)
sid_r = runtime_sta_cxx.read_syntax(syn_dump)
check("syntax dump->read round-trip", runtime_sta_cxx.dump_syntax(sid_r) == syn_dump)
check("syntax read dedup", sid_r == sid)
sid_rd = runtime_sta_cxx.read_syntax(syn)
check("syntax read(dict) round-trip", runtime_sta_cxx.dump_syntax(sid_rd) == syn_dump)
check("syntax read(dict) dedup", sid_rd == sid)
syn_path = tmp("binding_test_syntax.json")
runtime_sta_cxx.store_syntax(sid, syn_path)
sid_l = runtime_sta_cxx.load_syntax(syn_path)
check("syntax store->load round-trip", runtime_sta_cxx.dump_syntax(sid_l) == syn_dump)
check("syntax load dedup", sid_l == sid)
os.remove(syn_path)
# sid_r / sid_rd / sid_l each took a holder; release them. sid keeps its own.
runtime_sta_cxx.release_syntax(sid_r)
runtime_sta_cxx.release_syntax(sid_rd)
runtime_sta_cxx.release_syntax(sid_l)

scid = runtime_sta_cxx.load_search(SEARCH)
check("load_search returns str handle", isinstance(scid, str))
srch = runtime_sta_cxx.get_search(scid)
check("get_search returns dict", isinstance(srch, dict))
check("get_search has text section", "text" in srch)
srch_dump = runtime_sta_cxx.dump_search(scid)
scid_r = runtime_sta_cxx.read_search(srch_dump)
check("search dump->read round-trip", runtime_sta_cxx.dump_search(scid_r) == srch_dump)
check("search read dedup", scid_r == scid)
scid_rd = runtime_sta_cxx.read_search(srch)
check("search read(dict) round-trip", runtime_sta_cxx.dump_search(scid_rd) == srch_dump)
check("search read(dict) dedup", scid_rd == scid)
srch_path = tmp("binding_test_search.json")
runtime_sta_cxx.store_search(scid, srch_path)
scid_l = runtime_sta_cxx.load_search(srch_path)
check("search store->load round-trip", runtime_sta_cxx.dump_search(scid_l) == srch_dump)
check("search load dedup", scid_l == scid)
os.remove(srch_path)
# scid_r / scid_rd / scid_l each took a holder; release them. scid keeps its own.
runtime_sta_cxx.release_search(scid_r)
runtime_sta_cxx.release_search(scid_rd)
runtime_sta_cxx.release_search(scid_l)

# ===== fta — instantiate + full surface =====
print("\nfta:")
fta_id = runtime_sta_cxx.instantiate(pid, "test", {}, sid, scid)
check("instantiate returns str handle", isinstance(fta_id, str))
fta = runtime_sta_cxx.get_fta(fta_id)
check("get_fta returns dict", isinstance(fta, dict))
check("get_fta has actions", "actions" in fta)
fta_dump = runtime_sta_cxx.dump_fta(fta_id)
fta_r = runtime_sta_cxx.read_fta(fta_dump)
check("read_fta (str) round-trip", runtime_sta_cxx.dump_fta(fta_r) == fta_dump)
check("read_fta (str) dedup", fta_r == fta_id)
fta_r2 = runtime_sta_cxx.read_fta(fta)
check("read_fta (dict) round-trip", runtime_sta_cxx.dump_fta(fta_r2) == fta_dump)
check("read_fta (dict) dedup", fta_r2 == fta_id)
fta_path = tmp("binding_test_fta.json")
runtime_sta_cxx.store_fta(fta_id, fta_path)
check("store_fta wrote file", os.path.isfile(fta_path))
os.remove(fta_path)
# fta_r / fta_r2 each took a holder; release them. fta_id keeps its own.
runtime_sta_cxx.release_fta(fta_r)
runtime_sta_cxx.release_fta(fta_r2)
try:
    runtime_sta_cxx.instantiate(pid, "nonexistent", {}, sid, scid)
    check("instantiate bad prompt raises", False, "expected exception")
except Exception:
    check("instantiate bad prompt raises", True)
runtime_sta_cxx.release_fta(fta_id)

# ===== backend_llama_cxx + ftt =====
print("\nbackend_llama_cxx / ftt:")
check("rng vocab_size", backend_llama_cxx.vocab_size(0) == 258)
tokens = backend_llama_cxx.tokenize(0, "hello")
check("tokenize length", len(tokens) == 5)
check("detokenize roundtrip", backend_llama_cxx.detokenize(0, tokens) == "hello")
pid6 = compiler_stl_cxx.compile(f"{SHARE}/demos/mcq/select.stl")
content = {"topic": "Sci", "question": "2+2?", "choices": ["3", "4", "5", "6"]}
fta_id3 = runtime_sta_cxx.instantiate(pid6, "main", content, sid, scid)
ftt_id = backend_llama_cxx.evaluate(0, fta_id3)
check("evaluate returns str handle", isinstance(ftt_id, str))
ftt = runtime_sta_cxx.get_ftt(ftt_id)
check("get_ftt returns dict", isinstance(ftt, dict))
check("ftt has uid", "uid" in ftt)
check("ftt has text", "text" in ftt)
check("ftt has children", "children" in ftt)
check("ftt has tokens", "tokens" in ftt)
ftt_dump = runtime_sta_cxx.dump_ftt(ftt_id)
check("dump_ftt returns str", isinstance(ftt_dump, str) and len(ftt_dump) > 0)
ftt_r = runtime_sta_cxx.read_ftt(ftt_dump)
check("read_ftt round-trip", runtime_sta_cxx.dump_ftt(ftt_r) == ftt_dump)
check("read_ftt dedup", ftt_r == ftt_id)
# ftt_r took a holder; release it. ftt_id keeps its own (released below).
runtime_sta_cxx.release_ftt(ftt_r)
ftt_path = tmp("binding_test_ftt.json")
runtime_sta_cxx.store_ftt(ftt_id, ftt_path)
check("store_ftt wrote file", os.path.isfile(ftt_path))
os.remove(ftt_path)
frame = runtime_sta_cxx.walk_ftt_to_frame(pid6, "main", ftt_id, content)
check("walk_ftt_to_frame returns dict", isinstance(frame, dict))
runtime_sta_cxx.release_ftt(ftt_id)
runtime_sta_cxx.release_fta(fta_id3)

scid_full = runtime_sta_cxx.load_search(os.path.join(SHARE, "search", "full.json"))
fta_id4 = runtime_sta_cxx.instantiate(pid6, "main", content, sid, scid_full)
ftt_id2 = backend_llama_cxx.evaluate(0, fta_id4)
check("evaluate with full search", isinstance(ftt_id2, str))
runtime_sta_cxx.release_ftt(ftt_id2)
runtime_sta_cxx.release_fta(fta_id4)
runtime_sta_cxx.release_search(scid_full)

runtime_sta_cxx.release_syntax(sid)
runtime_sta_cxx.release_search(scid)
compiler_stl_cxx.release(pid6)

compiler_stl_cxx.release(pid)
try:
    runtime_sta_cxx.get_sta(pid)
    check("release invalidates handle", False, "expected exception")
except Exception:
    check("release invalidates handle", True)

# ===== logging =====
print("\nlogging:")
import logging
runtime_sta_cxx.set_log_level(10)
records = []
handler = logging.Handler()
handler.emit = lambda r: records.append(r)
logger = logging.getLogger("autocog")
logger.addHandler(handler)
logger.setLevel(1)
runtime_sta_cxx.set_log_level(20)
pid_log = compiler_stl_cxx.compile(os.path.join(FIXTURES, "language/structures/test_basic_record.stl"))
check("bridge sink receives INFO", len([r for r in records if r.levelno >= 20]) > 0)
records.clear()
runtime_sta_cxx.set_log_level(40)
pid_log2 = compiler_stl_cxx.compile(os.path.join(FIXTURES, "language/structures/test_basic_record.stl"))
check("bridge sink filters below ERROR", len([r for r in records if r.levelno < 40]) == 0)
logger.removeHandler(handler)
runtime_sta_cxx.set_log_level(30)
compiler_stl_cxx.release(pid_log)
compiler_stl_cxx.release(pid_log2)

# ===== errors =====
print("\nerrors:")
try:
    runtime_sta_cxx.load_syntax("/tmp/nonexistent_syntax_binding_test.json")
    check("FileError from missing syntax file", False, "no exception raised")
except Exception as e:
    check("FileError from missing syntax file",
          type(e).__name__ == "FileError" and hasattr(e, "path"))
try:
    runtime_sta_cxx.load_search("/tmp/nonexistent_search_binding_test.json")
    check("FileError from missing search file", False, "no exception raised")
except Exception as e:
    check("FileError from missing search file", type(e).__name__ == "FileError")
try:
    runtime_sta_cxx.load_sta("/tmp/nonexistent_program_binding_test.json")
    check("FileError from bad sta", False, "no exception raised")
except Exception as e:
    check("FileError from bad sta",
          type(e).__name__ == "FileError" and isinstance(e, OSError))
bad_pid = compiler_stl_cxx.compile(f"{FIXTURES}/errors/test_missing_semicolon.stl")
bad_diags = compiler_stl_cxx.get_diagnostics(bad_pid)
check("bad STL records error diagnostics", any(d.get("level") == "error" for d in bad_diags))
located = [d for d in bad_diags if d.get("location")]
check("diagnostic carries resolved location", bool(located))
if located:
    loc = located[0]["location"]
    check("diagnostic location has file/line/column",
          loc.get("file", "").endswith(".stl")
          and isinstance(loc.get("line"), int)
          and isinstance(loc.get("column"), int))
compiler_stl_cxx.release(bad_pid)

print()
if errors:
    print(f"FAILED: {len(errors)} errors")
    for e in errors:
        print(f"  {e}")
    sys.exit(1)
else:
    print("ALL PASSED")
    sys.exit(0)
