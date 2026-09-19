"""The harness's descriptor: groups, syntax specs, and the question draw.

share/experiments is a separate application -- the benchmarking harness,
not part of what AutoCog does -- so these run on their own:

    python3 -m pytest share/experiments/tests

They are deliberately NOT in the repo's pytest job, which gates the
library.
"""

import collections
import json
import os

import pytest

from autocog.bench import reuse
from conftest import layout_for


QUESTION = {"id": "q1", "topic": "T", "question": "What is H2O?",
            "choice": [{"label": "A", "value": "Water"},
                       {"label": "B", "value": "Fire"}],
            "answer": "Water"}

BASE = {"models": [{"tag": "rng"}],
        "datasets": [{"name": "d", "samples": 1}],
        "programs": {"a": {"filepath": "x.stl"}, "b": {"filepath": "y.stl"},
                     "c": {"filepath": "z.stl"}}}


def write(path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    if str(path).endswith(".ndjson"):
        path.write_text("".join(json.dumps(r) + "\n" for r in obj))
    else:
        path.write_text(json.dumps(obj))


@pytest.fixture(scope="module")
def rng_worker():
    """An in-process backend serving only the rng pseudo-model: enough to
    exercise the pooled path, which is the one campaigns take, without a
    GGUF and without a subprocess."""
    from autocog.server import backend as backend_srv
    from test_helpers import running_server

    with running_server(backend_srv.create_app(models=[])) as port:
        yield [f"127.0.0.1:{port}"]


def test_a_pool_too_small_to_draw_from_is_fatal(tmp_path, repo_root):
    """Silently short is the dangerous outcome: a campaign that quietly ran
    12 questions where the descriptor said 100 looks exactly like one that
    ran 100, until somebody reads the error bars."""
    from campaign.samples import SelectionError, select

    pool = [dict(QUESTION, id=f"q{i}", question=f"Q{i}?") for i in range(6)]
    data = tmp_path / "pool.json"
    write(data, pool)
    used = {reuse.question_digest(q) for q in pool[:4]}

    with pytest.raises(SelectionError, match="cannot supply 5"):
        select("pool", str(data), str(tmp_path / "out"), 5, exclude=used,
               log=lambda *_: None)


def test_selection_is_written_once_and_reread(tmp_path):
    """Idempotent: a campaign that died halfway must not re-draw, or the
    second half of the matrix answers different questions from the first."""
    from campaign.samples import select

    pool = [dict(QUESTION, id=f"q{i}", question=f"Q{i}?") for i in range(20)]
    data = tmp_path / "pool.json"
    write(data, pool)
    out = tmp_path / "results"

    path = select("pool", str(data), str(out), 5, log=lambda *_: None)
    chosen = json.load(open(path))
    assert len(chosen) == 5

    logs = []
    again = select("pool", str(data), str(out), 99, exclude={"anything"},
                   log=logs.append)
    assert again == path
    assert json.load(open(again)) == chosen           # not re-drawn
    assert any("already chosen" in m for m in logs)


def test_two_campaigns_over_one_dataset_do_not_intersect(tmp_path, repo_root):
    """The ask, end to end: campaign two draws questions campaign one did
    not use, so concatenating their results counts every sample once."""
    from campaign.samples import select

    pool = [dict(QUESTION, id=f"q{i}", question=f"Q{i}?") for i in range(50)]
    data = tmp_path / "pool.json"
    write(data, pool)

    one = json.load(open(select("pool", str(data), str(tmp_path / "c1"), 10,
                                log=lambda *_: None)))
    used = reuse.prior_questions([str(tmp_path / "c1")])
    two = json.load(open(select("pool", str(data), str(tmp_path / "c2"), 10,
                                exclude=used, log=lambda *_: None)))

    assert len(one) == len(two) == 10
    assert not {q["id"] for q in one} & {q["id"] for q in two}


def test_a_run_may_not_set_its_own_sample_count(tmp_path, repo_root):
    """`samples` moved to the dataset so every arm of a campaign is asked
    the same questions. A run that kept its own would make two arms
    incomparable while still looking like one matrix — refused, not
    ignored, because ignoring it runs the whole dataset instead."""
    from campaign.descriptor import Descriptor, DescriptorError

    base = {"models": [{"tag": "rng"}], "datasets": [{"name": "d"}],
            "programs": {"p": {"filepath": "x.stl"}}}
    Descriptor("ok", dict(base, runs=[
        {"name": "r", "model": "rng", "dataset": "d", "programs": ["p"]}]))
    with pytest.raises(DescriptorError, match="belongs to the dataset"):
        Descriptor("bad", dict(base, runs=[
            {"name": "r", "model": "rng", "dataset": "d", "programs": ["p"],
             "samples": 10}]))


def descriptor_from(tmp_path, data):
    from campaign.descriptor import load

    path = tmp_path / "c.yml"
    path.write_text(json.dumps(data))
    return load(str(path))


BASE = {"models": [{"tag": "rng"}],
        "datasets": [{"name": "d", "samples": 1}],
        "programs": {"a": {"filepath": "x.stl"}, "b": {"filepath": "y.stl"},
                     "c": {"filepath": "z.stl"}}}


def test_at_expands_a_group(tmp_path):
    """The same list is usually shared by several runs, and a list repeated
    is a list that drifts: two runs meant to be one contrast quietly stop
    being one when somebody edits a copy."""
    d = descriptor_from(tmp_path, dict(
        BASE,
        program_groups={"pair": ["a", "b"]},
        syntax_groups={"idx": ["s0", "s1"]},
        runs=[{"name": "r", "model": "rng", "dataset": "d",
               "programs": ["@pair", "c"], "syntaxes": ["@idx", "complete"]}]))
    assert d.runs[0]["programs"] == ["a", "b", "c"]
    assert d.runs[0]["syntaxes"] == ["s0", "s1", "complete"]


def test_an_unknown_group_is_fatal(tmp_path):
    from campaign.descriptor import DescriptorError

    with pytest.raises(DescriptorError, match="no program group 'typo'"):
        descriptor_from(tmp_path, dict(
            BASE, program_groups={"pair": ["a"]},
            runs=[{"name": "r", "model": "rng", "dataset": "d",
                   "programs": ["@typo"]}]))


def test_groups_do_not_nest(tmp_path):
    """A group of groups reads like an abstraction and behaves like an
    indirection nobody can follow when a run turns out to have the wrong
    arms."""
    from campaign.descriptor import DescriptorError

    with pytest.raises(DescriptorError, match="groups do not nest"):
        descriptor_from(tmp_path, dict(
            BASE, program_groups={"one": ["a"], "two": ["@one", "b"]},
            runs=[{"name": "r", "model": "rng", "dataset": "d",
                   "programs": ["@two"]}]))


def test_an_undeclared_syntax_is_a_shipped_file(tmp_path, repo_root):
    """`syntaxes: [complete]` must keep working with nothing declared —
    the mapping is for overrides and renames, not a registration tax."""
    from campaign.syntaxes import materialize_all

    d = descriptor_from(tmp_path, dict(
        BASE, runs=[{"name": "r", "model": "rng", "dataset": "d",
                     "programs": ["a"], "syntaxes": ["complete"]}]))
    assert d.syntax("complete") == {}

    paths, _ = materialize_all(d, layout_for(repo_root, tmp_path),
                            str(tmp_path / "res"), log=lambda *_: None)
    shipped = json.load(open(os.path.join(repo_root, "share", "syntax",
                                          "complete.json")))
    assert json.load(open(paths["complete"])) == shipped


def test_overrides_apply_to_the_base(tmp_path, repo_root):
    from campaign.syntaxes import materialize_all

    d = descriptor_from(tmp_path, dict(
        BASE,
        syntaxes={"i1-tab": {"filepath": "complete.json",
                             "prompt_zero_index": False,
                             "prompt_indent": "\t"}},
        runs=[{"name": "r", "model": "rng", "dataset": "d",
               "programs": ["a"], "syntaxes": ["i1-tab"]}]))
    paths, _ = materialize_all(d, layout_for(repo_root, tmp_path),
                            str(tmp_path / "res"), log=lambda *_: None)
    got = json.load(open(paths["i1-tab"]))
    shipped = json.load(open(os.path.join(repo_root, "share", "syntax",
                                          "complete.json")))
    assert got["prompt_zero_index"] is False
    assert got["prompt_indent"] == "\t"
    # Everything else is held fixed: that is the point of a base.
    assert {k: v for k, v in got.items()
            if k not in ("prompt_zero_index", "prompt_indent")} == \
           {k: v for k, v in shipped.items()
            if k not in ("prompt_zero_index", "prompt_indent")}


def test_a_local_syntax_beats_a_shipped_one(tmp_path, repo_root):
    """Resolution is descriptor-directory first. An operator's own file
    winning over a shipped one of the same name is the direction that
    surprises nobody."""
    from campaign.syntaxes import materialize_all

    (tmp_path / "complete.json").write_text('{"prompt_indent": "MINE"}')
    d = descriptor_from(tmp_path, dict(
        BASE, runs=[{"name": "r", "model": "rng", "dataset": "d",
                     "programs": ["a"], "syntaxes": ["complete"]}]))
    paths, _ = materialize_all(d, layout_for(repo_root, tmp_path),
                            str(tmp_path / "res"), log=lambda *_: None)
    assert json.load(open(paths["complete"])) == {"prompt_indent": "MINE"}


def test_an_override_of_a_field_the_base_lacks_is_fatal(tmp_path, repo_root):
    """A typo in a knob name is otherwise a no-op that looks like an arm:
    the campaign runs, two cells render identically, and the contrast the
    descriptor was written for never happened."""
    from campaign.syntaxes import SyntaxError_, materialize_all

    d = descriptor_from(tmp_path, dict(
        BASE,
        syntaxes={"oops": {"filepath": "complete.json",
                           "prompt_with_indices": True}},   # not a field
        runs=[{"name": "r", "model": "rng", "dataset": "d",
               "programs": ["a"], "syntaxes": ["oops"]}]))
    with pytest.raises(SyntaxError_, match="prompt_with_indices"):
        materialize_all(d, layout_for(repo_root, tmp_path),
                        str(tmp_path / "res"), log=lambda *_: None)


def test_a_syntax_name_is_not_a_path(tmp_path, repo_root):
    from campaign.syntaxes import SyntaxError_, materialize_all

    d = descriptor_from(tmp_path, dict(
        BASE, runs=[{"name": "r", "model": "rng", "dataset": "d",
                     "programs": ["a"], "syntaxes": ["grid/i0-typed"]}]))
    with pytest.raises(SyntaxError_, match="a name is a name, not a path"):
        materialize_all(d, layout_for(repo_root, tmp_path),
                        str(tmp_path / "res"), log=lambda *_: None)


def test_a_run_uses_the_resolved_syntax_not_a_shipped_name(tmp_path, repo_root,
                                                           rng_worker):
    """End to end: the name is the event's grouping key, and the file it
    denotes is the campaign's own resolved JSON — not share/syntax/<name>."""
    from campaign.syntaxes import materialize_all
    from autocog.bench.quality import run_quality

    d = descriptor_from(tmp_path, dict(
        BASE,
        syntaxes={"no-index": {"filepath": "complete.json",
                               "prompt_with_index": False}},
        runs=[{"name": "r", "model": "rng", "dataset": "d",
               "programs": ["a"], "syntaxes": ["no-index"]}]))
    paths, _ = materialize_all(d, layout_for(repo_root, tmp_path),
                            str(tmp_path / "res"), log=lambda *_: None)

    evs = run_quality(
        questions=1, syntaxes=["no-index"], syntax_paths=paths,
        root=str(repo_root),
        data=os.path.join(repo_root, "share", "benchmarks", "quality",
                          "questions.json"),
        programs=["share/demos/mcq/strings-select.stl"],
        out=str(tmp_path / "out"), workers=rng_worker, log=lambda *_: None)
    assert len(evs) == 1
    assert evs[0]["autocog.bench.syntax"] == "no-index"
    assert evs[0].get("error.message") is None


def test_labels_ride_along_and_stay_out_of_the_syntax_file(tmp_path, repo_root):
    """`labels` describes the ARM, not the rendering.

    A stray key in the written syntax is a field the C++ codec does not
    know, and the campaign would die on the box with a parse error long
    after this was the cheap thing to catch."""
    from campaign.syntaxes import materialize_all

    d = descriptor_from(tmp_path, dict(
        BASE,
        syntaxes={"i1-tab": {"filepath": "complete.json",
                             "labels": ["one-based", "indent-tab"],
                             "prompt_zero_index": False,
                             "prompt_indent": "\t"}},
        runs=[{"name": "r", "model": "rng", "dataset": "d",
               "programs": ["a"], "syntaxes": ["i1-tab"]}]))
    paths, labels = materialize_all(d, layout_for(repo_root, tmp_path),
                                    str(tmp_path / "res"), log=lambda *_: None)

    written = json.load(open(paths["i1-tab"]))
    assert "labels" not in written and "filepath" not in written
    assert written["prompt_zero_index"] is False

    # The written names, then a knob=value per knob the spec NAMES --
    # named, not "differs from the base": a cell whose value happens to
    # match the base is still part of that contrast and must not drop out
    # of the slice. JSON spelling, so a label never holds a raw tab.
    assert labels["i1-tab"] == ["one-based", "indent-tab",
                                "prompt_zero_index=false",
                                'prompt_indent="\\t"']
