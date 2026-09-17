"""Executing a descriptor's runs against workers that already exist.

Everything here is a thin call into `autocog bench`: the app resolves names to
paths and hands them over, the library runs the matrix. A run that fails is
isolated — the campaign reports it and carries on, because a dead arm is not
a reason to throw away the ones that would have worked.
"""

import os


def resolve_dataset(descriptor, layout, name):
    path = os.path.join(layout.datasets, f"{name}.json")
    if not os.path.isfile(path):
        raise FileNotFoundError(
            f"dataset {name!r} is not built: expected {path}")
    return path


def program_specs(descriptor, layout, run):
    """The run's programs with filepaths resolved. Relative paths are
    relative to the checkout's share/, which is where programs ship."""
    out = {}
    for name, spec in descriptor.programs_for(run).items():
        spec = dict(spec)
        path = spec.get("filepath", "")
        if path and not os.path.isabs(path):
            spec["filepath"] = layout.share(path)
        out[name] = spec
    return out


def run_one(descriptor, layout, run, urls, seed=42, out=None, log=print):
    """One run. Returns its events, or raises for the caller to isolate.

    `out` overrides the results directory. The validity gate uses that to
    write somewhere disposable: sharing the campaign's output would let four
    probe samples satisfy the resume index and the real run would find its
    work already done."""
    from autocog.bench.quality import run_quality

    out_dir = out or os.path.join(layout.results_for(descriptor.name), run["name"])
    os.makedirs(out_dir, exist_ok=True)
    model = descriptor.model(run["model"])
    return run_quality(
        model=run["model"] if run["model"] != "rng" else None,
        data=resolve_dataset(descriptor, layout, run["dataset"]),
        questions=run["samples"],
        syntaxes=run["syntaxes"],
        programs=program_specs(descriptor, layout, run),
        out=out_dir,
        # The app knows where the checkout is; the library should not have to
        # guess it by walking up from a working directory that is deliberately
        # somewhere else.
        root=layout.repo,
        seed=seed,
        n_ctx=model["ctx"] if model else 2048,
        workers=urls or None,
        log=log,
    )


def run_all(descriptor, layout, workers, events, seed=42, log=print):
    """Every run, in descriptor order. Returns the names that failed."""
    failures = []
    for run in descriptor.runs:
        urls = workers.urls_for(run["model"])
        if not urls:
            log(f"[{run['name']}] no worker hosts {run['model']!r} — skipping")
            events.emit("run.skip", descriptor.name, **{
                "autocog.campaign.run": run["name"],
                "error.message": f"no worker hosts {run['model']!r}"})
            failures.append(run["name"])
            continue
        events.emit("run.start", descriptor.name, **{
            "autocog.campaign.run": run["name"],
            "autocog.campaign.model": run["model"]})
        try:
            evs = run_one(descriptor, layout, run, urls, seed=seed,
                          log=lambda m, _n=run["name"]: log(f"[{_n}] {m}"))
            events.emit("run.end", descriptor.name, **{
                "autocog.campaign.run": run["name"],
                "autocog.campaign.samples": len(evs)})
        except Exception as e:           # one arm dying is not the campaign
            log(f"[{run['name']}] FAILED: {type(e).__name__}: {e}")
            events.emit("run.end", descriptor.name, **{
                "autocog.campaign.run": run["name"],
                "error.message": f"{type(e).__name__}: {e}"})
            failures.append(run["name"])
    return failures
