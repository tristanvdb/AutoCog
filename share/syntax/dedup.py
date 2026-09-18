#!/usr/bin/env python3
"""Which syntax cells actually render differently, per program.

    python share/syntax/dedup.py                       # the table
    python share/syntax/dedup.py --json classes.json   # the mapping

The grid is a product of knobs (see generate.py), and a product
over-counts: a knob that a program's schema never exercises renders
nothing. `prompt_indent` emits one prefix per NESTING LEVEL, so on a flat
schema it is inert and its four levels collapse to one. `prompt_zero_index`
sets the label base and the values a `select` offers, so under `repeat` --
where the candidates are texts, not positions -- it goes inert too once the
labels are unnumbered.

Measured on the mcq demos, that is 332 distinct cells where the grid names
576: 42% of a naive campaign would be the same measurement bought twice.
Which is why this is a pass over instantiated automata rather than a
paragraph of reasoning about the knobs -- the collapse depends on the
program's schema, and getting it wrong costs money in one direction and a
missing arm in the other.

WHAT IS COMPARED is the FTA's `actions` array, for EVERY prompt the program
has, with the inspection content supplied. Not the FTA's own hash: that
folds in provenance, including the syntax's hash, so it separates all 64
cells by construction and would report a 0% collapse very convincingly.

Rendering is model-free -- instantiate and read -- so this costs seconds and
can gate a campaign before anything is spent.
"""

import argparse
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
GRID = os.path.join(HERE, "grid")
DEMOS = os.path.join(REPO, "share", "demos", "mcq")
CONTENT = os.path.join(DEMOS, "inspect", "content.json")
STLIB = os.path.join(REPO, "share", "library", "stlib")
SEARCH = os.path.join(REPO, "share", "search", "default.json")

#: the MCQ programs a campaign draws its contrasts from: two shapes
#: (strings / record), the commit mechanisms, and two scaffolds
PROGRAMS = ["strings-select", "strings-repeat", "record-index", "record-value",
            "record-label", "record-index-annot", "record-value-annot",
            "strings-select-cot", "strings-select-iter"]


def syntaxes(grid=GRID):
    return [os.path.splitext(f)[0] for f in sorted(os.listdir(grid))
            if f.endswith(".json")]


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


def classify(programs=PROGRAMS, grid=GRID, log=print):
    """{program: {representative syntax: [every cell equal to it]}}."""
    import autocog
    from autocog.runtime.sta import runtime_sta_cxx as sta

    table = json.load(open(CONTENT))
    cells = syntaxes(grid)
    search_id = sta.load_search(SEARCH)
    classes = {}
    for demo in programs:
        program = autocog.compile(os.path.join(DEMOS, demo + ".stl"),
                                  includes=[STLIB])
        prompts = sorted(program.prompts)
        content, missing = content_for(demo, prompts, table)
        if missing:
            log(f"  note: {demo} has no inspection content for "
                f"{missing} — those prompts render empty")
        groups = {}
        for cell in cells:
            syntax_id = sta.load_syntax(os.path.join(grid, cell + ".json"))
            try:
                key = render_digest(program, syntax_id, search_id, content,
                                    prompts)
            finally:
                sta.release_syntax(syntax_id)
            groups.setdefault(key, []).append(cell)
        # The representative is the first cell in the grid's own order, so
        # the choice is stable across runs and machines.
        classes[demo] = {members[0]: members for members in groups.values()}
    return classes


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--program", action="append", default=None,
                    help=f"program to measure (repeatable; default: {PROGRAMS})")
    ap.add_argument("--grid", default=GRID, help="directory of syntax cells")
    ap.add_argument("--json", default=None,
                    help="write {program: [representative cells]} here")
    ap.add_argument("--expect", type=int, default=None,
                    help="fail unless the total is exactly this many cells")
    args = ap.parse_args()

    classes = classify(args.program or PROGRAMS, args.grid)
    n_cells = len(syntaxes(args.grid))

    total = 0
    print(f"{'program':<24} {'distinct':>12}   collapses")
    for demo, groups in classes.items():
        total += len(groups)
        collapsed = sorted({len(m) for m in groups.values()} - {1})
        print(f"{demo:<24} {len(groups):>5} / {n_cells:<4}   "
              + (f"classes of {collapsed}" if collapsed else "-"))
    naive = n_cells * len(classes)
    print(f"{'TOTAL':<24} {total:>5} / {naive:<4}   "
          f"{(naive - total) / naive:.0%} of the grid is duplicate")

    if args.json:
        with open(args.json, "w") as f:
            json.dump({d: sorted(g) for d, g in classes.items()}, f, indent=2)
            f.write("\n")
        print(f"wrote {args.json}")

    if args.expect is not None and total != args.expect:
        print(f"FAIL: expected {args.expect} distinct cells, got {total}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
