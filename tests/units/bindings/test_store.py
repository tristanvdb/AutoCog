#!/usr/bin/env python3
"""
Unit test for the binding datastore surface — binding only.

Imports the extension modules directly and never the autocog package, so it
exercises the bindings in isolation. For every tracked structure (syntax,
search, sta, fta, ftt) it drives the full six-verb store surface:

    load, read, get, store, dump, release

Each structure's artifact is produced within the test (compile / instantiate /
evaluate against the built-in RNG model), so every verb is checked against a
real, self-consistent artifact. Run under ctest with PYTHONPATH set to the
binding build directories.
"""

import sys
import os
import json
import tempfile

import compiler_stl_cxx
import runtime_sta_cxx
import backend_llama_cxx

SHARE  = sys.argv[1]
SYNTAX = os.path.join(SHARE, "syntax", "default.json")
SEARCH = os.path.join(SHARE, "search", "default.json")
SELECT = os.path.join(SHARE, "demos", "mcq", "select.stl")

errors = []


def check(name, cond, msg=""):
    if cond:
        print(f"  OK: {name}")
    else:
        errors.append(name)
        print(f"  FAIL: {name}: {msg}")


def tmp(name):
    return os.path.join(tempfile.gettempdir(), name)


def exercise(prefix, h0):
    """Drive all six store verbs for one structure given an initial handle."""
    get     = getattr(runtime_sta_cxx, f"get_{prefix}")
    read    = getattr(runtime_sta_cxx, f"read_{prefix}")
    dump    = getattr(runtime_sta_cxx, f"dump_{prefix}")
    store   = getattr(runtime_sta_cxx, f"store_{prefix}")
    load    = getattr(runtime_sta_cxx, f"load_{prefix}")
    release = getattr(runtime_sta_cxx, f"release_{prefix}")

    # get: datastore -> Python object (direct codec_python conversion)
    obj = get(h0)
    check(f"{prefix}: get -> Python object", obj is not None)

    # read: Python object -> datastore; identical content dedups to h0
    h_read = read(obj)
    check(f"{prefix}: read(get(h)) dedups to same handle", h_read == h0,
          f"{h_read} != {h0}")

    # dump: datastore -> JSON string
    text = dump(h0)
    check(f"{prefix}: dump -> non-empty string", isinstance(text, str) and len(text) > 0)
    try:
        json.loads(text)
        valid = True
    except Exception as e:
        valid = False
        check(f"{prefix}: dump parse error", False, str(e))
    check(f"{prefix}: dump is valid JSON", valid)

    # store: datastore -> JSON file; load it back; identical content dedups to h0
    path = tmp(f"unit_store_{prefix}.json")
    store(h0, path)
    check(f"{prefix}: store -> file written",
          os.path.isfile(path) and os.path.getsize(path) > 0)
    h_load = load(path)
    check(f"{prefix}: load(store(h)) dedups to same handle", h_load == h0,
          f"{h_load} != {h0}")
    os.remove(path)

    # release: h0, h_read, h_load are three holders on one artifact; drop all.
    release(h_read)
    release(h_load)
    release(h0)
    released = False
    try:
        get(h0)
    except Exception:
        released = True
    check(f"{prefix}: get after final release raises", released)


# --- produce one real artifact per structure, binding only ---
sid    = runtime_sta_cxx.load_syntax(SYNTAX)
scid   = runtime_sta_cxx.load_search(SEARCH)
pid    = compiler_stl_cxx.compile(SELECT)                          # STA
content = {"topic": "Sci", "question": "2+2?", "choices": ["3", "4", "5", "6"]}
fta_id = runtime_sta_cxx.instantiate(pid, "main", content, sid, scid)  # FTA
ftt_id = backend_llama_cxx.evaluate(0, fta_id)                     # FTT (RNG model 0)

for prefix, handle in [("syntax", sid), ("search", scid), ("sta", pid),
                       ("fta", fta_id), ("ftt", ftt_id)]:
    print(f"{prefix}:")
    exercise(prefix, handle)

if errors:
    print(f"\n{len(errors)} checks FAILED")
    sys.exit(1)
print("\nALL PASSED")
