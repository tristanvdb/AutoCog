"""Choosing a campaign's datapoints: once, per dataset, up front.

A campaign draws `samples` questions from each dataset it names, and
every run in it sees exactly those. That is the whole reason the choice
lives here and not inside a run: arms are only comparable if they were
asked the same questions, and a per-run draw makes "syntax A beat syntax
B" indistinguishable from "syntax A got the easier questions".

The selection is written out as a real dataset file,

    results/<campaign>/samples-<dataset>.json

in the same shape as the dataset it came from, and the runs are pointed
at it. So the campaign's question set is an artifact you can read, diff
and hand to someone — not a seed and a promise that the same code will
reproduce it.

`prior` subtracts: given earlier campaigns' directories, the questions
they used are removed from the pool BEFORE the draw, so the new campaign
still gets its full `samples` — from what is left. Two campaigns over
the same dataset therefore have disjoint question sets, which is what
makes their results safe to union. Nothing is copied forward; a row
appearing in two campaigns would be counted twice by every consumer that
concatenates them.

Idempotent: an existing selection file is used as-is. A campaign that
died halfway and is re-run must not re-draw, or the second half of the
matrix answers different questions from the first.
"""

import json
import os

from autocog.bench.formatters import load_formatter, load_questions


class SelectionError(Exception):
    pass


def selection_path(results_dir, dataset):
    return os.path.join(results_dir, f"samples-{dataset}.json")


def select(dataset, source_path, results_dir, samples, exclude=(),
           formatter="", log=print):
    """Choose (or re-read) this campaign's questions for one dataset.

    Returns the path to the selection file."""
    dest = selection_path(results_dir, dataset)
    if os.path.isfile(dest):
        with open(dest) as f:
            have = json.load(f)
        log(f"samples: {dataset} — {len(have)} question(s) already chosen")
        return dest

    pool = load_questions(source_path, load_formatter(formatter))
    chosen = load_questions(source_path, load_formatter(formatter),
                            limit=samples, exclude=exclude)
    if samples and len(chosen) < samples:
        raise SelectionError(
            f"dataset {dataset!r} cannot supply {samples} unused question(s): "
            f"{len(pool)} in the dataset, {len(pool) - len(chosen)} already "
            f"used by the prior campaign(s). Point `prior` elsewhere, raise "
            f"the dataset's `limit`, or lower `samples`.")

    os.makedirs(results_dir, exist_ok=True)
    tmp = dest + ".part"
    with open(tmp, "w") as f:
        json.dump(chosen, f, indent=1)
        f.write("\n")
    os.replace(tmp, dest)
    log(f"samples: {dataset} — chose {len(chosen)} of {len(pool)}"
        + (f", avoiding {len(exclude)} already used" if exclude else ""))
    return dest


def select_all(descriptor, layout, results_dir, prior_dirs=(), log=print):
    """Every dataset any run names. Returns {dataset name: selection path}.

    Only datasets a run actually uses are drawn: a descriptor may list
    more, and drawing from one nothing reads would burn questions the next
    campaign could have had.
    """
    from autocog.bench.reuse import prior_questions

    from . import runner

    wanted = {run["dataset"] for run in descriptor.runs}
    exclude = prior_questions(prior_dirs, log=log) if prior_dirs else set()

    out = {}
    for name in sorted(wanted):
        spec = descriptor.dataset(name)
        out[name] = select(
            name, runner.resolve_dataset(descriptor, layout, name),
            results_dir, int(spec.get("samples") or 0), exclude=exclude,
            log=log)
    return out
