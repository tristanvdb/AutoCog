"""bench campaign — execute a campaign descriptor against workers.

A campaign is a DESCRIPTOR (data, owned by the operator, not stored in
the repo) describing experiments; hardware is not its concern beyond a
worker *mode* that a launcher interprets. This executor is a pure
consumer of workers: it NEVER spawns, configures or stops them — it is
always given `--worker` URLs (level-3 backends preassigned with models)
and round-robins each run to a compatible worker by model tag.

Descriptor:
    {
      "name": "v08-protocol",
      "defaults": {"seed": 42, "questions": 0},
      "worker": {"mode": "distributed", "factor": 3},   // launcher concern
      "models": [{"model": "Llama-3.2-1B", "ctx": 8192}, ...],  // launcher concern
      "datasets": ["arc-easy"],                          // launcher concern
      "phases": [
        {"name": "protocol",
         "runs": [
           {"kind": "quality", "model": "Llama-3.2-1B", "data": "arc-easy",
            "syntaxes": ["complete"], "demos": ["select", "repeat"]},
           {"kind": "perf", "model": "Llama-3.1-8B-Instruct",
            "cells": "termination"}
         ]}
      ]
    }

The executor reads `phases` (optionally filtered by --phase) and
`defaults`; `worker`/`models`/`datasets` are launcher sections it
ignores. Run kinds: quality, perf ("probe" runs are a launcher concern
— this executor refuses them). A run's `model` is a TAG: the workers
were loaded by the launcher, paths are not this layer's business.

Name resolution (the experiments workdir convention, env-overridable):
    data   -> $DATASETS_PATH | $AUTOCOG_WORKDIR/datasets | ./datasets, name[.json]
    cells  -> <repo>/share/benchmarks/compute/, cells-<name>.json | <name>.json
    out    -> $RESULTS_PATH | $AUTOCOG_WORKDIR/results | ./results, /<campaign>/<run out>

Resume: a run whose output directory already holds a results-*.ndjson
is skipped, so re-invoking after an interruption continues the campaign.
"""

import glob
import json
import os

from autocog.errors import ConfigError

from . import results
from .workers import RemoteWorker


def workdir():
    return os.environ.get("AUTOCOG_WORKDIR") or os.getcwd()


def resolve_data(name):
    """Dataset name -> file path (absolute paths and existing relatives win)."""
    if os.path.isfile(name):
        return name
    base = os.environ.get("DATASETS_PATH") or os.path.join(workdir(), "datasets")
    for cand in (os.path.join(base, name), os.path.join(base, name + ".json"),
                 os.path.join(base, name + ".jsonl")):
        if os.path.isfile(cand):
            return cand
    raise ConfigError(f"dataset {name!r} not found under {base}")


def resolve_repo():
    repo = os.environ.get("AUTOCOG_REPO")
    if repo:
        return repo
    for cand in (os.path.join(workdir(), "autocog"), workdir()):
        if os.path.isdir(os.path.join(cand, "share", "syntax")):
            return cand
    raise ConfigError("cannot locate the repo (set AUTOCOG_REPO)")


def resolve_cells(name):
    """Cells name -> file under the repo's share/benchmarks/compute."""
    if os.path.isfile(name):
        return name
    base = os.path.join(resolve_repo(), "share", "benchmarks", "compute")
    for cand in (os.path.join(base, f"cells-{name}.json"),
                 os.path.join(base, f"{name}.json")):
        if os.path.isfile(cand):
            return cand
    raise ConfigError(f"cells {name!r} not found under {base}")


def results_root(campaign_name):
    base = os.environ.get("RESULTS_PATH") or os.path.join(workdir(), "results")
    return os.path.join(base, campaign_name)


class WorkerPool:
    """The --worker URLs, probed once; round-robin routing by model tag."""

    def __init__(self, urls):
        self.workers = [RemoteWorker(u) for u in urls]
        self._rr = {}
        for w in self.workers:
            w.capabilities()   # fail fast on unreachable workers

    def pick(self, tag):
        compatible = [w for w in self.workers
                      if tag in w.capabilities()["models"]]
        if not compatible:
            hosted = {w.url: w.capabilities()["models"] for w in self.workers}
            raise ConfigError(f"no worker hosts model {tag!r}: {hosted}")
        i = self._rr.get(tag, 0)
        self._rr[tag] = i + 1
        return compatible[i % len(compatible)]


def run_done(out_dir):
    return bool(glob.glob(os.path.join(out_dir, "results-*.ndjson")))


def execute_run(run, defaults, pool, out_dir, log):
    kind = run.get("kind")
    params = dict(defaults)
    params.update({k: v for k, v in run.items() if k not in ("kind", "out")})
    tag = params.pop("model", None) or "rng"
    worker = pool.pick(tag)

    if kind == "quality":
        from .quality import run_quality
        data = resolve_data(params.pop("data"))
        run_quality(model=tag, data=data,
                    formatter=params.pop("formatter", ""),
                    questions=int(params.pop("questions", 0) or 0),
                    syntaxes=params.pop("syntaxes", None),
                    demos=params.pop("demos", None),
                    out=out_dir, root=resolve_repo(),
                    seed=int(params.pop("seed", 42)),
                    workers=[worker.url], log=log)
    elif kind == "perf":
        from .perf import run_perf
        cells = resolve_cells(params.pop("cells"))
        run_perf(model=tag, cells=cells, out=out_dir,
                 tag=params.pop("tag", ""),
                 budget_seconds=float(params.pop("budget_seconds", 0) or 0),
                 seed=int(params.pop("seed", 42)),
                 workers=[worker.url], log=log)
    elif kind == "probe":
        raise ConfigError("probe runs are a campaign-launcher concern; "
                          "this executor only runs quality/perf")
    else:
        raise ConfigError(f"unknown run kind: {kind!r}")


def run_campaign(descriptor_path, phases=None, workers=None, log=print):
    """Execute a campaign descriptor's phases against the given workers."""
    desc = json.load(open(descriptor_path))
    name = desc.get("name") or os.path.splitext(
        os.path.basename(descriptor_path))[0]
    if not workers:
        raise ConfigError("bench campaign requires --worker URL(s); worker "
                          "lifecycle belongs to the campaign launcher")
    pool = WorkerPool(workers)
    defaults = desc.get("defaults", {})
    root = results_root(name)

    selected = [p for p in desc.get("phases", [])
                if not phases or p.get("name") in phases]
    if phases and len(selected) != len(phases):
        known = [p.get("name") for p in desc.get("phases", [])]
        raise ConfigError(f"phase(s) {phases} not in {known}")

    def lifecycle(action, phase, label, run, **extra):
        ev = {"event.action": action, "autocog.campaign.name": name,
              "autocog.campaign.phase": phase,
              "autocog.campaign.run": label,
              "autocog.campaign.kind": run.get("kind"),
              "autocog.campaign.model": run.get("model", "rng")}
        ev.update(extra)
        results.stream_event(ev)

    failures = []
    for phase in selected:
        pname = phase.get("name", "phase")
        for i, run in enumerate(phase.get("runs", [])):
            label = run.get("out") or f"{pname}-{i}"
            out_dir = os.path.join(root, label)
            if run_done(out_dir):
                log(f"[{pname}/{label}] output exists — skipping")
                lifecycle("run.skip", pname, label, run)
                continue
            os.makedirs(out_dir, exist_ok=True)
            log(f"=== [{pname}/{label}] {run.get('kind')} "
                f"model={run.get('model', 'rng')} ===")
            lifecycle("run.start", pname, label, run)
            try:
                execute_run(run, defaults, pool, out_dir, log)
                lifecycle("run.end", pname, label, run, **{"autocog.campaign.ok": True})
            except Exception as e:  # noqa: BLE001 — isolate runs
                failures.append(f"{pname}/{label}")
                log(f"!!! {pname}/{label} failed — continuing: {e}")
                lifecycle("run.end", pname, label, run,
                          **{"autocog.campaign.ok": False,
                             "error.message": str(e)[:300]})

    if failures:
        log(f"[failed] {len(failures)} run(s): " + ", ".join(failures))
    log(f"campaign phase(s) complete: {root}")
    return root, failures
