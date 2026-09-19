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


def resolve_prior(layout, names):
    """Earlier-results directories, by campaign name or by path.

    A name that resolves to nothing RAISES. `prior` exists so two campaigns
    do not draw the same questions, and a typo that quietly degraded into
    "avoid nothing" fails in the one way the operator cannot see: the
    campaign runs, and its results silently overlap the ones they were
    meant to extend."""
    out = []
    for name in names:
        if os.path.isdir(name):
            out.append(os.path.abspath(name))
            continue
        path = layout.results_for(name)
        if not os.path.isdir(path):
            raise FileNotFoundError(
                f"prior results {name!r} not found: neither a directory nor "
                f"a campaign under {layout.results}")
        out.append(path)
    return out


def run_one(descriptor, layout, run, urls, seed=42, out=None, data=None,
            questions=0, syntax_paths=None, syntax_labels=None,
            log=print):
    """One run. Returns its events, or raises for the caller to isolate.

    `data` is the campaign's SELECTION for this run's dataset (see
    samples.py) — already drawn, so the run takes all of it and chooses
    nothing. `questions` truncates it, which only the validity gate does.

    `out` overrides the results directory. The gate uses that to write
    somewhere disposable: sharing the campaign's output would let four
    probe samples satisfy the resume index and the real run would find its
    work already done."""
    from autocog.bench.quality import run_quality

    out_dir = out or os.path.join(layout.results_for(descriptor.name), run["name"])
    os.makedirs(out_dir, exist_ok=True)
    model = descriptor.model(run["model"])
    return run_quality(
        model=run["model"] if run["model"] != "rng" else None,
        data=data or resolve_dataset(descriptor, layout, run["dataset"]),
        questions=questions,
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
        syntax_paths=syntax_paths,
        syntax_labels=syntax_labels,
        log=log,
    )


def run_all(descriptor, layout, workers, events, seed=42, selections=None,
            syntax_paths=None, syntax_labels=None, log=print):
    """Every run, in descriptor order. Returns the names that failed.

    `selections` maps a dataset name to this campaign's chosen questions;
    `syntax_paths` / `syntax_labels` map a syntax name to its resolved
    JSON and its labels."""
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
                          data=(selections or {}).get(run["dataset"]),
                          syntax_paths=syntax_paths,
                          syntax_labels=syntax_labels,
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
