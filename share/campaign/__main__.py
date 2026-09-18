"""Run a campaign.

    autocog/share/campaign.sh my-campaign.yml

Stages, each idempotent, so a campaign that died halfway resumes by being run
again: provision -> probe -> plan -> workers up -> run -> workers down.

The app provisions; `autocog bench` consumes. Nothing here evaluates a prompt,
and nothing in the library downloads a model, spawns a worker or knows where
results go.
"""

import argparse
import json
import os
import sys

from . import plan as planning
from . import preflight as preflighting
from . import probe as probing
from . import provision, runner
from .descriptor import DescriptorError, load
from .events import Events, attach_bench_stream
from .layout import Layout


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python3 -m campaign",
                                 description=__doc__.split("\n\n")[0])
    ap.add_argument("descriptor", help="campaign descriptor (YAML or JSON)")
    ap.add_argument("--workdir", default=None,
                    help="where artifacts live (default: the invocation "
                         "directory, or the checkout's parent when invoked "
                         "from inside it)")
    ap.add_argument("--dry-run", action="store_true",
                    help="provision nothing, spawn nothing: print the plan")
    ap.add_argument("--mode", choices=["isolated", "distributed"], default=None,
                    help="override the measured worker topology")
    ap.add_argument("--ngl", type=int, default=int(os.environ.get("AUTOCOG_NGL", 99)),
                    help="GPU layers per worker")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--only", action="append", default=None,
                    help="restrict to these run names (repeatable)")
    ap.add_argument("--skip-gate", action="store_true",
                    help="run without the answer-validity check (say why)")
    ap.add_argument("--prior", action="append", default=None,
                    help="earlier campaign whose samples this one may adopt "
                         "instead of re-measuring: a name under results/, or "
                         "a path (repeatable; adds to the descriptor's own "
                         "`prior`)")
    args = ap.parse_args(argv)

    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    repo = os.path.dirname(repo)             # share/campaign -> share -> repo
    layout = Layout(repo, args.workdir)

    try:
        descriptor = load(args.descriptor)
    except (OSError, DescriptorError) as e:
        sys.exit(f"descriptor: {e}")

    if args.only:
        wanted = set(args.only)
        descriptor.runs = [r for r in descriptor.runs if r["name"] in wanted]
        if not descriptor.runs:
            sys.exit(f"no run matches {sorted(wanted)}")

    log = print
    log(f"=== campaign {descriptor.name} ===")
    log(f"workdir: {layout.workdir}")

    # Resolved before the plan is printed, so a --dry-run says how much of
    # the campaign is already paid for and a bad name costs nothing.
    try:
        prior_dirs = runner.resolve_prior(layout,
                                          descriptor.prior + (args.prior or []))
    except FileNotFoundError as e:
        sys.exit(f"prior: {e}")

    machine = probing.load_profile(layout) or probing.scan_machine()
    slots = planning.plan(descriptor, layout, machine, mode=args.mode)
    missing = planning.unhosted(slots, descriptor)

    log(f"machine: {len(machine.get('gpus') or [])} GPU(s)")
    for slot in slots:
        log(f"  worker {slot.index}: gpu={slot.gpu} "
            f"models={[m['tag'] for m in slot.models] or ['rng']}")
    for run in descriptor.runs:
        log(f"  run {run['name']}: model={run['model']} "
            f"dataset={run['dataset']} programs={run['programs']} "
            f"syntaxes={run['syntaxes']} samples={run['samples'] or 'all'}")
    if missing:
        sys.exit(f"no worker can host {missing} — the campaign would stall")

    prior_index = None
    if prior_dirs:
        from autocog.bench.reuse import PriorIndex

        # Once for the campaign: a screening campaign's results run to
        # gigabytes, and every run would otherwise re-read all of them.
        prior_index = PriorIndex(prior_dirs, log=log)
        for d in prior_dirs:
            log(f"  prior: {d}")

    # Every arm, once, on rng — before the weights are downloaded, because a
    # descriptor that cannot execute should cost nothing to find out about.
    if not args.skip_gate:
        failures = preflighting.check(descriptor, layout, log=log)
        if failures:
            log(f"preflight: {len(failures)} arm(s) do not execute")
            preflighting.report(failures, log)
            sys.exit("preflight failed. Nothing was provisioned and nothing "
                     "was spent. --skip-gate runs anyway if you know why.")
        log("preflight: every arm executes")

    if args.dry_run:
        log("dry run: nothing provisioned, nothing spawned")
        return 0

    layout.ensure()
    probing.save_profile(layout, machine)

    try:
        provision.fetch_models(descriptor, layout, log=log)
        provision.build_datasets(descriptor, layout, log=log)
    except provision.ProvisionError as e:
        sys.exit(f"provision: {e}")

    results = layout.results_for(descriptor.name)
    os.makedirs(results, exist_ok=True)
    events_path = os.path.join(layout.results, "campaign-events.ndjson")
    events = Events(events_path)
    attach_bench_stream(events_path)
    events.emit("campaign.plan", descriptor.name, **{
        "autocog.campaign.runs": len(descriptor.runs),
        "axes": ["model", "syntax", "program"],
        "labels": descriptor.labels(),
        "workers": [s.url for s in slots]})

    from .workers import WorkerFailed, Workers

    try:
        with Workers(slots, layout, ngl=args.ngl, log=log) as workers:
            events.emit("campaign.start", descriptor.name)
            if not args.skip_gate:
                try:
                    _gate(descriptor, layout, workers, log)
                except probing.GateFailure as e:
                    events.emit("campaign.end", descriptor.name,
                                **{"error.message": f"gate: {e}"})
                    sys.exit(f"validity gate: {e}\n"
                             f"Nothing was spent. Fix the program or the data; "
                             f"--skip-gate runs anyway if you know why.")
            failures = runner.run_all(descriptor, layout, workers, events,
                                      seed=args.seed, prior=prior_index,
                                      log=log)
    except WorkerFailed as e:
        sys.exit(f"workers: {e}")

    events.emit("campaign.end", descriptor.name,
                **{"autocog.campaign.failures": failures})
    log(f"=== {descriptor.name} done -> {results} ===")
    if failures:
        log(f"failed runs: {failures}")
        return 1
    return 0


def _gate(descriptor, layout, workers, log):
    """A few real samples per run, checked for usable answers before the
    campaign commits to the whole matrix."""
    from autocog.bench.formatters import load_formatter, load_questions

    log("gate: checking answers are in range and vary")
    for run in descriptor.runs[:1] or []:
        urls = workers.urls_for(run["model"])
        if not urls:
            continue
        data = runner.resolve_dataset(descriptor, layout, run["dataset"])
        probe_run = dict(run, samples=4, syntaxes=run["syntaxes"][:1])
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            events = list(runner.run_one(descriptor, layout, probe_run, urls,
                                         out=tmp, log=lambda *_: None))
        questions = load_questions(data, load_formatter(""), limit=4)
        report = probing.check_answers(events, questions)
        log(f"gate: {report['scored']} scored, "
            f"{report['distinct']} distinct answer(s) — ok")


if __name__ == "__main__":
    sys.exit(main())
