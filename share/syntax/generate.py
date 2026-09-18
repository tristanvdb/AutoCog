#!/usr/bin/env python3
"""Generate the syntax grid from a declarative spec.

    python share/syntax/generate.py            # write share/syntax/grid/
    python share/syntax/generate.py --check    # verify, write nothing

The grid varies four rendering knobs against one base and writes a file per
cell. They are generated rather than hand-written because 64 near-identical
JSON files drift the moment one of them is edited by hand, and because the
space is meant to grow: adding a knob is adding a line to AXES.

The axes are only the ones being explored now. `special` (special-token
rendering) and the chat templates are deliberately out: the first campaign is
base models, which is what AutoCog was for in the first place.

WHAT THE KNOBS ACTUALLY DO, since two of them are not what the name suggests:

  prompt_with_index   `[i]` on a list field's label.
  prompt_zero_index   the label's base AND the candidate values a `select`
                      offers. It therefore still bites with the label
                      unnumbered: the model is then asked for a 1-based
                      position of a list that shows no positions.
  prompt_with_format  `(text(40))` on the label.
  prompt_indent       one prefix per nesting level, so it does NOTHING on a
                      flat schema. Only programs with a nested record show it.
  desc_inline         a `# ...` comment line per field, emitted once even for
                      an array.

The last two mean cells collapse per program: measured on the mcq demos, the
64 cells give 12 distinct renderings for flat+repeat and 64 for nested+select.
Confirm the real space with a digest pass before spending anything on it.
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.join(HERE, "complete.json")
OUT = os.path.join(HERE, "grid")

#: axis -> [(tag, {syntax keys})]. Tags compose into the filename in this
#: order, so a name says exactly which cell it is.
AXES = [
    ("index", [
        ("i0", {"prompt_with_index": True,  "prompt_zero_index": True}),
        ("i1", {"prompt_with_index": True,  "prompt_zero_index": False}),
        ("n0", {"prompt_with_index": False, "prompt_zero_index": True}),
        ("n1", {"prompt_with_index": False, "prompt_zero_index": False}),
    ]),
    ("typed", [
        ("typed", {"prompt_with_format": True}),
        ("bare",  {"prompt_with_format": False}),
    ]),
    ("indent", [
        ("flat", {"prompt_indent": ""}),
        ("sp2",  {"prompt_indent": "  "}),
        ("gt",   {"prompt_indent": "> "}),
        ("tab",  {"prompt_indent": "\t"}),
    ]),
    ("desc", [
        ("nodesc", {"desc_inline": False}),
        ("desc",   {"desc_inline": True}),
    ]),
]


def cells():
    """Every cell as (name, overrides), in a stable order."""
    def walk(axes):
        if not axes:
            yield [], {}
            return
        (_, levels), rest = axes[0], axes[1:]
        for tag, keys in levels:
            for tail_tags, tail_keys in walk(rest):
                yield [tag] + tail_tags, {**keys, **tail_keys}
    for tags, keys in walk(AXES):
        yield "-".join(tags), keys


def build(base):
    return {name: dict(base, **keys) for name, keys in cells()}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="compare against what is on disk; write nothing")
    args = ap.parse_args()

    with open(BASE) as f:
        base = json.load(f)
    grid = build(base)

    if args.check:
        stale = []
        for name, syn in grid.items():
            path = os.path.join(OUT, name + ".json")
            if not os.path.isfile(path):
                stale.append(f"missing: {name}.json")
                continue
            with open(path) as f:
                if json.load(f) != syn:
                    stale.append(f"differs: {name}.json")
        extra = [f for f in (os.listdir(OUT) if os.path.isdir(OUT) else [])
                 if f.endswith(".json") and f[:-5] not in grid]
        stale += [f"not in the spec: {f}" for f in sorted(extra)]
        if stale:
            print("share/syntax/grid is out of date with generate.py:")
            for s in stale:
                print("   ", s)
            print("Regenerate with: python share/syntax/generate.py")
            return 1
        print(f"OK: {len(grid)} grid syntaxes match the spec")
        return 0

    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".json") and f[:-5] not in grid:
            os.remove(os.path.join(OUT, f))
    for name, syn in grid.items():
        with open(os.path.join(OUT, name + ".json"), "w") as f:
            json.dump(syn, f, indent=2)
            f.write("\n")
    print(f"wrote {len(grid)} syntaxes to {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
