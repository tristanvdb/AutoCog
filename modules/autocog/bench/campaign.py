"""bench campaign — execute a campaign descriptor against workers.

A campaign is a DESCRIPTOR (data, owned by the operator, not stored in
the repo) describing experiments; hardware is not its concern beyond a
worker *mode* that a launcher interprets. This executor is a pure
consumer of workers: it NEVER spawns, configures or stops them — it is
always given `--worker` URLs (level-3 backends preassigned with models).

Quality runs execute at SAMPLE granularity: a phase's (run, syntax,
demo, question) samples are scheduled across the workers hosting each
run's tag through EnginePools (per-FTA lane dispatch), the scan keeping
every lane primed — see quality.run_samples. Perf runs keep an
exclusive single worker and execute sequentially, before the phase's
quality samples start (timing never shares a lane).

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

Resume: per SAMPLE for quality runs — existing result lines are indexed
by (syntax, demo, question) and only the gaps run, whatever writer
vintage produced them. Perf runs resume per run (results file finalized
on completion only).
"""

import asyncio
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


def hosted_map(urls):
    """url -> hosted tags, probed once (fail fast on unreachable workers)."""
    return {w.url: w.capabilities()["models"]
            for w in (RemoteWorker(u) for u in urls)}


def urls_for(hosted, tag):
    urls = [u for u, models in hosted.items() if tag in models]
    if not urls:
        raise ConfigError(f"no worker hosts model {tag!r}: {hosted}")
    return urls


def run_done(out_dir):
    """Perf-run resume: the results file only carries its final name on
    completion (quality runs resume per sample instead — see quality)."""
    return bool(glob.glob(os.path.join(out_dir, "results-*.ndjson")))


def run_tag(run, defaults):
    return run.get("model") or defaults.get("model") or "rng"


def run_campaign(descriptor_path, phases=None, workers=None, log=print):
    """Execute a campaign descriptor's phases against the given workers."""
    from .quality import QualityRun, run_samples

    desc = json.load(open(descriptor_path))
    name = desc.get("name") or os.path.splitext(
        os.path.basename(descriptor_path))[0]
    if not workers:
        raise ConfigError("bench campaign requires --worker URL(s); worker "
                          "lifecycle belongs to the campaign launcher")
    hosted = hosted_map(workers)
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

    def fail(pname, label, run, err):
        failures.append(f"{pname}/{label}")
        log(f"!!! {pname}/{label} failed — continuing: {err}")
        lifecycle("run.end", pname, label, run,
                  **{"autocog.campaign.ok": False,
                     "error.message": str(err)[:300]})

    def params_of(run):
        p = dict(defaults)
        p.update({k: v for k, v in run.items() if k not in ("kind", "out")})
        p.pop("model", None)
        return p

    for phase in selected:
        pname = phase.get("name", "phase")
        perf_runs, quality_runs = [], []
        for i, run in enumerate(phase.get("runs", [])):
            label = run.get("out") or f"{pname}-{i}"
            kind = run.get("kind")
            if kind == "perf":
                perf_runs.append((run, label))
            elif kind == "quality":
                quality_runs.append((run, label))
            else:
                fail(pname, label, run,
                     "probe runs are a campaign-launcher concern; this "
                     "executor only runs quality/perf" if kind == "probe"
                     else f"unknown run kind: {kind!r}")

        # Perf first, sequential, one exclusive worker each — nothing else
        # is in flight yet, so timing never shares a lane.
        for run, label in perf_runs:
            out_dir = os.path.join(root, label)
            if run_done(out_dir):
                log(f"[{pname}/{label}] output exists — skipping")
                lifecycle("run.skip", pname, label, run)
                continue
            log(f"=== [{pname}/{label}] perf model={run_tag(run, defaults)} ===")
            lifecycle("run.start", pname, label, run)
            try:
                from .perf import run_perf
                params = params_of(run)
                tag = run_tag(run, defaults)
                url = urls_for(hosted, tag)[0]
                os.makedirs(out_dir, exist_ok=True)
                run_perf(model=tag, cells=resolve_cells(params.pop("cells")),
                         out=out_dir, tag=params.pop("tag", ""),
                         budget_seconds=float(params.pop("budget_seconds", 0) or 0),
                         seed=int(params.pop("seed", 42)),
                         workers=[url],
                         log=lambda m, _l=label: log(f"[{pname}/{_l}] {m}"))
                lifecycle("run.end", pname, label, run,
                          **{"autocog.campaign.ok": True})
            except Exception as e:  # noqa: BLE001 — isolate runs
                fail(pname, label, run, e)

        # Quality at sample granularity across all compatible workers.
        entries, ledgers, meta = [], {}, []
        from autocog.remote import lane_ledger
        for run, label in quality_runs:
            out_dir = os.path.join(root, label)
            try:
                params = params_of(run)
                tag = run_tag(run, defaults)
                urls = urls_for(hosted, tag)
                os.makedirs(out_dir, exist_ok=True)
                qrun = QualityRun(
                    tag, resolve_data(params.pop("data")), out_dir,
                    resolve_repo(), formatter=params.pop("formatter", ""),
                    questions=int(params.pop("questions", 0) or 0),
                    syntaxes=params.pop("syntaxes", None),
                    demos=params.pop("demos", None),
                    log=lambda m, _l=label: log(f"[{pname}/{_l}] {m}"))
                if not qrun.pending:
                    log(f"[{pname}/{label}] all samples present — skipping")
                    lifecycle("run.skip", pname, label, run)
                    continue
                if tag not in ledgers:
                    ledgers[tag] = lane_ledger(urls)
                pools = qrun.make_pools(urls, ledgers[tag])
                seed = int(params.pop("seed", 42))
                for pool in pools.values():
                    pool.set_seed(seed)
                entries.append((qrun, pools))
                meta.append((run, label))
            except Exception as e:  # noqa: BLE001 — isolate runs
                fail(pname, label, run, e)
        if entries:
            log(f"=== [{pname}] {sum(len(e[0].pending) for e in entries)} "
                f"sample(s) over {sum(l.total_lanes() for l in ledgers.values())}"
                f" lane(s) ===")
            asyncio.run(run_samples(
                entries, ledgers, log=log,
                on_start=lambda i: lifecycle("run.start", pname, meta[i][1],
                                             meta[i][0]),
                on_end=lambda i: lifecycle("run.end", pname, meta[i][1],
                                           meta[i][0],
                                           **{"autocog.campaign.ok": True})))

    if failures:
        log(f"[failed] {len(failures)} run(s): " + ", ".join(failures))
    log(f"campaign phase(s) complete: {root}")
    return root, failures
