"""The campaign preflight: every arm, once, on rng, before anything is spent.

The gate this backs up looked at four real samples of ONE run's first
syntax. A 332-arm descriptor generated from the syntax grid had 42 arms
that raised out of the program's own resolver, and that gate would have
passed every time — it never touched them.
"""

import json
import os
import sys

import pytest


@pytest.fixture(autouse=True)
def from_repo_root(repo_root, monkeypatch):
    monkeypatch.chdir(repo_root)
    share = os.path.join(repo_root, "share")
    if share not in sys.path:
        sys.path.insert(0, share)


#: complete.json with the index base flipped. `select` emits a POSITION
#: and this decides whether it is spelled from 0 or from 1.
ZERO = {"filepath": "complete.json", "prompt_zero_index": True}
ONE = {"filepath": "complete.json", "prompt_zero_index": False}


def descriptor(tmp_path, syntaxes, programs=("strings-select",)):
    """`syntaxes` is {name: spec}; specs default to the shipped file."""
    from campaign.descriptor import load

    path = tmp_path / "c.yml"
    path.write_text(json.dumps({
        "models": [{"tag": "rng", "file": "rng"}],
        "datasets": [{"name": "q", "samples": 1}],
        "programs": {p: {"filepath": f"demos/mcq/{p}.stl", "labels": [p]}
                     for p in programs},
        "syntaxes": syntaxes,
        "runs": [{"name": "r", "model": "rng", "dataset": "q",
                  "programs": list(programs),
                  "syntaxes": list(syntaxes)}]}))
    return load(str(path))


def resolved(descriptor, repo_root, tmp_path):
    from campaign.syntaxes import materialize_all

    return materialize_all(descriptor, layout_for(repo_root, tmp_path),
                           str(tmp_path / "syn"), log=lambda *_: None)


def layout_for(repo_root, tmp_path):
    from campaign.layout import Layout

    return Layout(str(repo_root), str(tmp_path / "work"))


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


def test_an_arm_that_cannot_execute_is_caught(tmp_path, repo_root):
    """THE REGRESSION. `select` emits a POSITION, and prompt_zero_index
    decides whether that position is spelled from 0 or from 1 — but the
    demos' resolver (share/demos/mcq/mcq.py) indexes the choice list
    directly. Under a one-indexed syntax every answer names the next choice
    along, and the last one raises.

    Pinned here as failing until the index base reaches the resolver.
    When it does, this test flips to passing and should be rewritten
    against an arm that is broken on purpose.
    """
    from campaign import preflight

    desc = descriptor(tmp_path, {"zero": ZERO, "one": ONE})
    failures = preflight.check(desc, layout_for(repo_root, tmp_path),
                               log=lambda *_: None,
                               syntax_paths=resolved(desc, repo_root, tmp_path))
    assert [s for _, s, _ in failures] == ["one"]
    assert "IndexError" in failures[0][2]


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
