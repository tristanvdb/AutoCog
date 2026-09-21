#!/usr/bin/env python3
"""Which of a campaign's syntaxes actually render differently, per program.

    python3 share/experiments/dedup.py my-campaign.yml

A syntax grid is a product of knobs, and a product over-counts: a knob
that a program's schema never exercises renders nothing. `prompt_indent` emits one prefix per NESTING LEVEL, so on a flat
schema it is inert and its four levels collapse to one. `prompt_zero_index`
sets the label base and the values a `select` offers, so under `repeat` --
where the candidates are texts, not positions -- it goes inert too once the
labels are unnumbered.

Measured on the mcq demos over a 64-cell index/typed/indent/desc grid,
that is 332 distinct cells where the product names 576: 42% of a naive
campaign would be the same measurement bought twice. Which is why this is
a pass over instantiated automata rather than a paragraph of reasoning
about the knobs -- the collapse depends on the program's schema, and
getting it wrong costs money in one direction and a missing arm in the
other.

WHAT IS COMPARED is the FTA's `actions` array, for EVERY prompt the program
has, with the inspection content supplied. Not the FTA's own hash: that
folds in provenance, including the syntax's hash, so it separates all 64
cells by construction and would report a 0% collapse very convincingly.

Rendering is model-free -- instantiate and read -- so this costs seconds and
can check a descriptor before anything is spent.
"""

import argparse
import hashlib
import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))     # share/experiments
REPO = os.path.dirname(os.path.dirname(HERE))
DEMOS = os.path.join(REPO, "share", "demos", "mcq")
CONTENT = os.path.join(DEMOS, "inspect", "content.json")
STLIB = os.path.join(REPO, "share", "library", "stlib")
SEARCH = os.path.join(REPO, "share", "search", "default.json")

#: the MCQ programs a campaign draws its contrasts from: two shapes
#: (strings / record), the commit mechanisms, and two scaffolds
PROGRAMS = ["strings-select", "strings-repeat", "record-index", "record-value",
            "record-label", "record-index-annot", "record-value-annot",
            "strings-select-cot", "strings-select-iter"]


def from_descriptor(path):
    """{program: {syntax name: resolved JSON}} -- the ARMS a descriptor runs.

    Per program, not a cross product: a run pairs its programs with its own
    syntaxes, and checking every program against every syntax in the file
    would report duplicates in pairings nobody asked for.

    The descriptor is the only place a campaign's syntaxes exist now: they
    are a base file plus overrides, not a directory of files. Reading them
    from here is what keeps this honest -- it measures what will run.
    """
    sys.path.insert(0, HERE)
    from campaign.descriptor import load
    from campaign.layout import Layout
    from campaign import syntaxes as syntaxing

    desc = load(path)
    layout = Layout(REPO, os.path.dirname(os.path.abspath(path)))
    local = os.path.dirname(os.path.abspath(path))
    cache = {}

    def syntax(name):
        if name not in cache:
            cache[name] = syntaxing.resolve(name, desc.syntax(name) or {},
                                            layout, local)
        return cache[name]

    arms = {}
    for run in desc.runs:
        for program in run["programs"]:
            cells = arms.setdefault(program, {})
            for name in run["syntaxes"]:
                cells.setdefault(name, syntax(name))
    return arms


def render_digest(program, syntax_id, search_id, content, prompts):
    """A digest of what this syntax makes this program's prompts look like.

    Every prompt, because two cells can agree on `main` and differ on a
    sub-prompt -- the annotator demos have one, and a campaign that deduped
    on the entry prompt alone would drop arms that do differ.
    """
    from autocog.runtime.sta import runtime_sta_cxx as sta

    h = hashlib.sha256()
    for name in prompts:
        fta_id = sta.instantiate(program.id, name, content.get(name, {}),
                                 syntax_id, search_id)
        try:
            actions = sta.get_fta(fta_id)["actions"]
        finally:
            sta.release_fta(fta_id)
        h.update(name.encode())
        h.update(json.dumps(actions, sort_keys=True).encode())
    return h.hexdigest()


def content_for(demo, prompts, table):
    """The inspection content, keyed `<demo>.<prompt>` in content.json.

    A prompt with no entry renders with empty content, which still
    instantiates; it just exercises fewer knobs, so a missing entry can only
    UNDER-count distinctness. Reported rather than tolerated silently.
    """
    out, missing = {}, []
    for name in prompts:
        key = f"{demo}.{name}"
        if key in table:
            out[name] = table[key]
        else:
            missing.append(name)
    return out, missing


def classify(arms, log=print):
    """{program: {representative syntax: [every name equal to it]}}.

    `arms` is {program: {syntax name: syntax dict}}."""
    import autocog
    from autocog.runtime.sta import runtime_sta_cxx as sta

    table = json.load(open(CONTENT))
    search_id = sta.load_search(SEARCH)
    classes = {}
    for demo, cells in arms.items():
        program = autocog.compile(os.path.join(DEMOS, demo + ".stl"),
                                  includes=[STLIB])
        prompts = sorted(program.prompts)
        content, missing = content_for(demo, prompts, table)
        if missing:
            log(f"  note: {demo} has no inspection content for "
                f"{missing} — those prompts render empty")
        groups = {}
        for cell, syntax in cells.items():
            with tempfile.NamedTemporaryFile("w", suffix=".json") as f:
                json.dump(syntax, f)
                f.flush()
                syntax_id = sta.load_syntax(f.name)
            try:
                key = render_digest(program, syntax_id, search_id, content,
                                    prompts)
            finally:
                sta.release_syntax(syntax_id)
            groups.setdefault(key, []).append(cell)
        # The representative is the first name in the descriptor's order, so
        # the choice is stable across runs and machines.
        classes[demo] = {members[0]: members for members in groups.values()}
    return classes


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("descriptor", help="campaign descriptor (YAML or JSON)")
    ap.add_argument("--program", action="append", default=None,
                    help="only these programs (repeatable)")
    ap.add_argument("--strict", action="store_true",
                    help="exit non-zero if any two syntaxes collapse")
    args = ap.parse_args()

    arms = from_descriptor(args.descriptor)
    if args.program:
        arms = {p: c for p, c in arms.items() if p in set(args.program)}
    classes = classify(arms)

    total = naive = 0
    print(f"{'program':<24} {'distinct':>12}   collapses")
    for demo, groups in classes.items():
        total += len(groups)
        naive += len(arms[demo])
        collapsed = sorted({len(m) for m in groups.values()} - {1})
        print(f"{demo:<24} {len(groups):>5} / {len(arms[demo]):<4}   "
              + (f"classes of {collapsed}" if collapsed else "-"))
    print(f"{'TOTAL':<24} {total:>5} / {naive:<4}   "
          + (f"{(naive - total) / naive:.0%} duplicate" if naive else "-"))

    if total < naive:
        print("\nthe duplicates, per program:")
        for demo, groups in classes.items():
            for rep, members in groups.items():
                if len(members) > 1:
                    print(f"  {demo}: {', '.join(members)}")
    if args.strict and total < naive:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
