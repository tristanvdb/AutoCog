"""bench campaign — a manifest of runs, executed in order.

Manifest (JSON):
    {
      "name": "v0.8-accuracy",
      "out": "results/my-campaign",          // default: results/<name>
      "runs": [
        {"kind": "perf", "model": "models/x.gguf", "cells": "cells/e1.json",
         "tag": "e1", "budget_seconds": 2500},
        {"kind": "quality", "model": "models/x.gguf",
         "data": "datasets/arc.json", "formatter": "fmt.py:to_mcq",
         "questions": 100, "syntaxes": ["complete", "stripped"],
         "demos": ["select", "repeat"], "out": "acc-x"}
      ]
    }

Relative paths resolve against the manifest's directory. A failed run is
reported and the campaign continues ("workers" is reserved for the
distributed phase).
"""

import json
import os

from .perf import run_perf
from .quality import run_quality


def _resolve(base, value):
    if isinstance(value, str) and not os.path.isabs(value):
        cand = os.path.join(base, value)
        if os.path.exists(cand) or not os.path.exists(value):
            return cand
    return value


def run_campaign(manifest_path, log=print):
    base = os.path.dirname(os.path.abspath(manifest_path))
    manifest = json.load(open(manifest_path))
    name = manifest.get("name", "campaign")
    out = _resolve(base, manifest.get("out", os.path.join("results", name)))
    os.makedirs(out, exist_ok=True)

    failures = []
    for i, run in enumerate(manifest.get("runs", [])):
        kind = run.get("kind")
        label = run.get("tag") or run.get("out") or f"run-{i}"
        log(f"=== [{i + 1}/{len(manifest['runs'])}] {kind}: {label} ===")
        try:
            params = {k: v for k, v in run.items() if k != "kind"}
            for key in ("model", "cells", "data", "formatter"):
                if key in params and isinstance(params[key], str) and params[key]:
                    if key == "formatter" and ":" in params[key]:
                        fpath, fn = params[key].rsplit(":", 1)
                        params[key] = f"{_resolve(base, fpath)}:{fn}"
                    elif key != "formatter":
                        params[key] = _resolve(base, params[key])
            params["out"] = os.path.join(out, params.get("out", label))
            params["log"] = log
            if kind == "perf":
                run_perf(**params)
            elif kind == "quality":
                run_quality(**params)
            else:
                raise ValueError(f"unknown run kind: {kind!r}")
        except Exception as e:  # noqa: BLE001 — isolate runs like experiments do
            failures.append(label)
            log(f"!!! {label} failed — continuing: {e}")

    if failures:
        log(f"[failed] {len(failures)} run(s): " + ", ".join(failures))
    log(f"campaign complete: {out}")
    return out
