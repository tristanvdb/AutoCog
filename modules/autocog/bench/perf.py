"""bench perf — computational cells through the in-process worker.

The share/benchmarks/compute/sweep.py semantics (same cells JSON, same event
shape, same budget behavior) with evaluation through the bindings: no
per-cell process spawn, no per-cell model reload unless the cell changes
load-time parameters (slots/ctx), and the perf fields taken from the
evaluation response itself.
"""

import itertools
import json
import os
import time

import autocog
from autocog.errors import ConfigError

from . import results
from .workers import LocalWorker, pick_worker

DEFAULT_CONTENT = {
    "topic": "Science",
    "question": "What is H2O?",
    "choices": ["Water", "Fire", "Air", "Earth"],
}

FULL_MATRIX = {"beams": [1, 2, 4, 8], "ahead": [1, 2, 4], "width": [1, 2]}


def cell_content(cell):
    content = dict(DEFAULT_CONTENT)
    content.update(cell.get("content", {}))
    for field, n in cell.get("content_pad", {}).items():
        pad = " ".join(["lorem"] * int(n))
        value = content.get(field, "")
        if isinstance(value, list):
            content[field] = [f"{v} {pad}".strip() for v in value]
        else:
            content[field] = f"{value} {pad}".strip()
    return content


def search_config(cell):
    queue = {"metric": cell.get("metric", ["perplexity"])}
    # Termination predicate, in the TermExpr JSON object form the search
    # codec accepts (e.g. {"ge": ["terminals", 8]}); reaches the FTA as
    # queue.stop, so cells can sweep early-termination policies.
    if cell.get("stop") is not None:
        queue["stop"] = cell["stop"]
    return {
        "text": {
            "threshold": cell.get("threshold", 0.1),
            "beams": cell.get("beams", 4),
            "topk": cell.get("topk"),
            "ahead": cell.get("ahead", 1),
            "width": cell.get("width", 1),
            "repetition": cell.get("repetition"),
            "diversity": None,
        },
        "enum":   {"threshold": 0.1, "width": 1},
        "branch": {"threshold": 0.1, "width": 1},
        "flow":   {"threshold": 0.1, "width": 1},
        "queue":  queue,
    }


def cell_name(cell):
    if cell.get("label"):
        return cell["label"]
    parts = [f"b{cell.get('beams', 4)}", f"a{cell.get('ahead', 1)}", f"w{cell.get('width', 1)}"]
    for key in ("topk", "threshold", "slots"):
        if cell.get(key) is not None:
            parts.append(f"{key[0]}{cell[key]}")
    return " ".join(parts)


def find_root(start):
    """Walk up from `start` to the enclosing repo/workdir: the first
    ancestor holding share/syntax (relative cell paths resolve there)."""
    d = os.path.abspath(start)
    while True:
        if os.path.isdir(os.path.join(d, "share", "syntax")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            return None
        d = parent


def resolve(path, bases):
    if os.path.isabs(path):
        return path
    for base in bases:
        if base and os.path.exists(os.path.join(base, path)):
            return os.path.join(base, path)
    raise ConfigError(f"cannot resolve {path!r} against {bases}")


CELL_KEYS = ("label", "beams", "ahead", "width", "topk", "threshold",
             "repetition", "metric", "stop", "slots", "stl", "syntax", "ctx")


def run_perf(model=None, cells=None, out=".", tag="", budget_seconds=0,
             ctx=2048, seed=42, quick=False, workers=None, log=print):
    """Run perf cells; returns the list of summary events."""
    if cells:
        cell_list = json.load(open(cells))
        bases = [os.getcwd(), os.path.dirname(os.path.abspath(cells)),
                 find_root(os.path.dirname(os.path.abspath(cells)))]
    else:
        matrix = FULL_MATRIX if not quick else {"beams": [1, 4], "ahead": [1, 2], "width": [1]}
        cell_list = [dict(zip(matrix.keys(), combo))
                     for combo in itertools.product(*matrix.values())]
        bases = [os.getcwd(), find_root(os.getcwd())]

    root = next((b for b in bases if b and os.path.isdir(os.path.join(b, "share", "syntax"))),
                None)
    if root is None:
        raise ConfigError("cannot locate share/syntax from cwd or the cells file")

    model_tag = os.path.splitext(os.path.basename(model))[0] if model else "rng"
    stamp = f"{results.host_name()}-{model_tag}" + (f"-{tag}" if tag else "")
    os.makedirs(out, exist_ok=True)
    nd = results.NdjsonWriter(os.path.join(out, f"results-{stamp}.ndjson"))

    remote = pick_worker(workers, model) if workers else None
    if remote:
        caps = remote.capabilities()
        log(f"worker: {remote.url} hosts {caps['models']} "
            f"(cpus={caps.get('cpus')}, n_ctx={caps.get('n_ctx')})")
    local_workers = {}   # (slots, ctx) -> LocalWorker
    programs = {}        # stl path -> Program

    def worker_for(cell):
        key = (cell.get("slots"), cell.get("ctx", ctx))
        if remote:
            # Models (and their load-time params) are pre-assigned on remote
            # workers: a cell demanding different slots/ctx cannot be honored.
            if key[0] is not None and key[0] != caps.get("kv_slots"):
                raise ConfigError(
                    f"cell wants kv_slots={key[0]} but worker has {caps.get('kv_slots')}")
            if cell.get("ctx") is not None and cell["ctx"] > caps.get("n_ctx", 0):
                raise ConfigError(
                    f"cell wants ctx={cell['ctx']} but worker loaded n_ctx={caps.get('n_ctx')}")
            return remote
        if key not in local_workers:
            local_workers[key] = LocalWorker(model=model, n_ctx=key[1],
                                             kv_slots=key[0])
        return local_workers[key]

    def program_for(stl):
        if stl not in programs:
            programs[stl] = autocog.compile(stl, includes=[os.path.dirname(stl)])
        return programs[stl]

    from autocog.runtime.sta import runtime_sta_cxx

    summaries, failed = [], []
    t_start = time.time()
    for i, cell in enumerate(cell_list):
        if budget_seconds and (time.time() - t_start) > budget_seconds:
            log(f"[budget] {budget_seconds:.0f}s exhausted — skipping "
                f"{len(cell_list) - i} remaining cell(s): "
                + ", ".join(cell_name(c) for c in cell_list[i:]))
            break
        try:
            stl = resolve(cell.get("stl") or "share/benchmarks/compute/benchmark.stl", bases)
            syntax = resolve(cell.get("syntax") or "share/syntax/complete.json", bases)
            wk = worker_for(cell)
            engine = wk.engine_for_search_config(syntax, search_config(cell))
            prog = program_for(stl)

            wk.reset()
            wk.set_seed(seed)
            content = cell_content(cell)
            t0 = time.time()
            fta_id = runtime_sta_cxx.instantiate(
                prog.id, "main", content, engine.syntax_id, engine.search_id)
            try:
                perf = wk.evaluate_fta(fta_id)
            finally:
                runtime_sta_cxx.release_fta(fta_id)
            wall = time.time() - t0

            summary = results.base_event("autocog.bench.perf", "eval.summary")
            summary.update(perf)
            for k in CELL_KEYS:
                if cell.get(k) is not None:
                    summary[f"autocog.bench.{k}"] = cell[k]
            for k, v in (("beams", 4), ("ahead", 1), ("width", 1)):
                summary.setdefault(f"autocog.bench.{k}", v)
            summary["autocog.bench.wall_seconds"] = round(wall, 3)
            summary["autocog.bench.model"] = model_tag
            summaries.append(summary)
            nd.emit(summary)
            log(f"[{i + 1}/{len(cell_list)}] {cell_name(cell)} -> "
                f"{summary['autocog.perf.advance_seconds']:.2f}s eval, "
                f"restore={summary['autocog.perf.tokens.restore']} "
                f"eval={summary['autocog.perf.tokens.eval']} "
                f"decode_calls={summary.get('autocog.perf.decode.calls', '-')}")
        except Exception as e:  # noqa: BLE001 — a failed cell must not stop the run
            failed.append(cell_name(cell))
            log(f"[{i + 1}/{len(cell_list)}] {cell_name(cell)} FAILED: {e}")

    nd.close()
    if failed:
        log(f"[failed] {len(failed)} cell(s): " + ", ".join(failed))
    if summaries:
        md = os.path.join(out, f"results-{stamp}.md")
        results.perf_md(md, f"{results.host_name()} — {model_tag}"
                        + (f" — {tag}" if tag else ""),
                        summaries, os.environ.get("AUTOCOG_NGL", "0"))
        log(f"\nresults: {nd.path}\n         {md}")
    return summaries
