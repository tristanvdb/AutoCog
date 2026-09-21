"""The campaign preflight: every arm, once, on rng, before anything is spent.

The gate this backs up looked at four real samples of ONE run's first
syntax. A 332-arm descriptor generated from the syntax grid had 42 arms
that raised out of the program's own resolver, and that gate would have
passed every time — it never touched them.
"""

import json
import os

import pytest

from conftest import layout_for


#: complete.json with the index base flipped. `select` emits a POSITION
#: and this decides whether it is spelled from 0 or from 1.
ZERO = {"filepath": "complete.json", "prompt_zero_index": True}
ONE = {"filepath": "complete.json", "prompt_zero_index": False}

#: an arm that renders fine and dies on the return (fixtures/broken.stl)
BROKEN = {"filepath": os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "fixtures", "broken.stl")}


def descriptor(tmp_path, syntaxes, programs=("strings-select",)):
    """`syntaxes` is {name: spec}; `programs` are mcq demo names, or
    (name, spec) pairs for anything else."""
    from campaign.descriptor import load

    specs = {}
    for p in programs:
        if isinstance(p, tuple):
            specs[p[0]] = p[1]
        else:
            specs[p] = {"filepath": f"demos/mcq/{p}.stl", "labels": [p]}
    programs = list(specs)

    path = tmp_path / "c.yml"
    path.write_text(json.dumps({
        "models": [{"tag": "rng", "file": "rng"}],
        "datasets": [{"name": "q", "samples": 1}],
        "programs": specs,
        "syntaxes": syntaxes,
        "runs": [{"name": "r", "model": "rng", "dataset": "q",
                  "programs": list(programs),
                  "syntaxes": list(syntaxes)}]}))
    return load(str(path))


def resolved(descriptor, repo_root, tmp_path):
    from campaign.syntaxes import materialize_all

    paths, _ = materialize_all(descriptor, layout_for(repo_root, tmp_path),
                               str(tmp_path / "syn"), log=lambda *_: None)
    return paths


def test_arms_are_deduped_across_model_runs(tmp_path, repo_root):
    """The same (program, syntax) pair appears once per model arm, and an
    rng answer says nothing about which model would have run it — checking
    it twice is only slower."""
    from campaign import preflight

    desc = descriptor(tmp_path, {"complete": {}, "stripped": {}},
                      ("strings-select", "record-value"))
    desc.runs = desc.runs + [dict(desc.runs[0], name="r2")]
    got = preflight.arms(desc, layout_for(repo_root, tmp_path))
    assert len(got) == 4                       # 2 programs x 2 syntaxes
    assert len({(n, s) for n, _, s in got}) == 4


def test_a_working_arm_passes(tmp_path, repo_root):
    from campaign import preflight

    desc = descriptor(tmp_path, {"complete": {}})
    assert preflight.check(desc, layout_for(repo_root, tmp_path),
                           log=lambda *_: None,
                           syntax_paths=resolved(desc, repo_root,
                                                 tmp_path)) == []


def test_both_index_bases_execute(tmp_path, repo_root):
    """THE REGRESSION, now inverted.

    `select` emits a POSITION and prompt_zero_index decides whether that
    position is spelled from 0 or from 1 — it sets the candidate values,
    not just the `[i]` on a label. The demos' resolver used to index the
    choice list directly, so under a one-indexed syntax every answer named
    the next choice along and the last one raised; 88 of this campaign's
    332 arms were affected and only 42 announced it.

    The base now reaches the resolver as a call binding
    (`__syntax__.prompt_zero_index`), so both render and both resolve."""
    from campaign import preflight

    desc = descriptor(tmp_path, {"zero": ZERO, "one": ONE})
    assert preflight.check(desc, layout_for(repo_root, tmp_path),
                           log=lambda *_: None,
                           syntax_paths=resolved(desc, repo_root,
                                                 tmp_path)) == []


def test_an_arm_that_cannot_execute_is_caught(tmp_path, repo_root):
    """The gate itself: an arm that dies must be reported, not swallowed.

    Broken on purpose — a stop token the renderer never emits, so the
    field never terminates."""
    from campaign import preflight

    desc = descriptor(tmp_path, {"complete": {}},
                      ("strings-select", ("broken", BROKEN)))
    failures = preflight.check(desc, layout_for(repo_root, tmp_path),
                               log=lambda *_: None,
                               syntax_paths=resolved(desc, repo_root, tmp_path))
    assert [name for name, _, _ in failures] == ["broken"], failures
    assert "this arm cannot execute" in failures[0][2]


def test_report_groups_by_defect(tmp_path, repo_root, capsys):
    """32 arms failing one way is one defect. A list of 32 lines reads
    like 32 defects, and the operator triages the wrong thing."""
    from campaign import preflight

    lines = []
    grouped = preflight.report([
        ("record-index", "grid/i1-bare-flat-desc", "IndexError: out of range"),
        ("record-index", "grid/i1-bare-flat-nodesc", "IndexError: out of range"),
        ("record-index", "grid/n1-bare-flat-desc", "ValueError: nope"),
    ], log=lines.append)
    assert len(grouped) == 2
    assert any("2 arm(s) fail with IndexError" in m for m in lines)
    assert any("1 arm(s) fail with ValueError" in m for m in lines)
