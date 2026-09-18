"""Building a campaign's syntaxes: a base file plus overrides.

A syntax is a flat JSON of rendering knobs, and an experiment usually
varies two or three of them. Shipping a file per combination does not
scale — four knobs over one base is 64 files that say almost the same
thing, and a directory of them in the repo is a generated artifact
pretending to be source. So a descriptor names a base and states what it
changes:

    syntaxes:
      i1-typed-tab:
        filepath: complete.json
        prompt_zero_index: false
        prompt_indent: "\\t"

`filepath` resolves beside the DESCRIPTOR first, then in the checkout's
share/syntax — so an operator's own syntax wins over a shipped one of the
same name, which is the direction that surprises nobody. Every other key
overrides the loaded file.

It defaults to `<name>.json`, which is what makes `syntaxes: [complete]`
work with nothing declared. There is deliberately no way to write a whole
syntax inline: a syntax is twenty-odd fields, nobody writes one by hand,
and every spec having a base is what lets an override of a field the base
does not have be an error rather than a knob that silently does nothing.

The resolved syntaxes are written to

    results/<campaign>/syntax/<name>.json

and the runs are pointed at those, for the same reason the questions are
(see samples.py): what a campaign actually ran should be an artifact you
can read and diff, not a descriptor plus the promise that the same code
would rebuild it. It is also what makes a name mean something — that
string is `autocog.bench.syntax` on every result row.

Unlike the question selection, this is rewritten on every launch. The
override is cheap and deterministic, and if the descriptor changed, the
sample fingerprint notices and those arms re-measure. Freezing it would
mean an edited descriptor silently running the old rendering.
"""

import json
import os

#: keys of a spec that are not syntax fields
META = ("filepath",)


class SyntaxError_(Exception):
    pass


def resolve(name, spec, layout, local_dir):
    """One spec -> the syntax dict it denotes."""
    if not isinstance(spec, dict):
        raise SyntaxError_(f"syntax {name!r}: expected a mapping, got {spec!r}")

    filepath = spec.get("filepath") or f"{name}.json"
    found = None
    for cand in (os.path.join(local_dir, filepath),
                 layout.share("syntax", filepath)):
        if os.path.isfile(cand):
            found = cand
            break
    if found is None:
        raise SyntaxError_(
            f"syntax {name!r}: no file {filepath!r} beside the descriptor "
            f"({local_dir}) or in {layout.share('syntax')}")
    with open(found) as f:
        base = json.load(f)

    overrides = {k: v for k, v in spec.items() if k not in META}
    # A typo in a knob name would otherwise be a no-op that looks like an
    # arm: the campaign runs, the two cells render identically, and the
    # contrast the descriptor was written for never happened.
    unknown = sorted(set(overrides) - set(base))
    if unknown:
        raise SyntaxError_(
            f"syntax {name!r}: {unknown} not in {filepath} — an override "
            f"of a field the base does not have is a knob that does "
            f"nothing, which reads as an arm that ran")
    base.update(overrides)
    return base


def materialize_all(descriptor, layout, results_dir, log=print):
    """Resolve every syntax any run names. Returns {name: path}."""
    wanted = sorted({s for run in descriptor.runs for s in run["syntaxes"]})
    local = os.path.dirname(os.path.abspath(descriptor.path or "."))
    out_dir = os.path.join(results_dir, "syntax")
    os.makedirs(out_dir, exist_ok=True)

    out = {}
    for name in wanted:
        if "/" in name or os.path.sep in name:
            raise SyntaxError_(
                f"syntax {name!r}: a name is a name, not a path — declare it "
                f"under `syntaxes:` with a `filepath`")
        syntax = resolve(name, descriptor.syntax(name) or {}, layout, local)
        path = os.path.join(out_dir, f"{name}.json")
        with open(path, "w") as f:
            json.dump(syntax, f, indent=2)
            f.write("\n")
        out[name] = path
    log(f"syntaxes: {len(out)} resolved -> {out_dir}")
    return out
