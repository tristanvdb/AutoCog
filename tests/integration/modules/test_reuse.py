"""Sample identity, at its two granularities.

The SAMPLE fingerprint decides whether the rows in a run's own output
describe the experiment it is about to run. The QUESTION digest decides
which datapoints a campaign draws — subtracting what an earlier campaign
already used, so their sample sets do not intersect and their results can
be unioned without double-counting anything.
"""

import collections
import json
import os
import shutil
import sys

import pytest

from autocog.bench import reuse


@pytest.fixture(autouse=True)
def from_repo_root(repo_root, monkeypatch):
    monkeypatch.chdir(repo_root)
    # The campaign app is not installed: it ships in share/, run in place.
    share = os.path.join(repo_root, "share")
    if share not in sys.path:
        sys.path.insert(0, share)


QUESTION = {"id": "q1", "topic": "T", "question": "What is H2O?",
            "choice": [{"label": "A", "value": "Water"},
                       {"label": "B", "value": "Fire"}],
            "answer": "Water"}


def fp(**over):
    args = dict(model="rng", syntax="SYN", search="SEA", program="PRG",
                question=QUESTION)
    args.update(over)
    return reuse.fingerprint(**args)


def test_every_input_moves_the_fingerprint():
    """Each component is load-bearing. A digest that ignores one of these
    is a digest that will one day say two different experiments are the
    same experiment."""
    base = fp()
    assert fp(model="Llama-3.2-1B") != base
    assert fp(syntax="other") != base
    assert fp(search="other") != base
    assert fp(program="other") != base
    assert fp(question=dict(QUESTION, question="What is CO2?")) != base
    # The id is not identity: two datasets both name something `q1`.
    assert fp(question=dict(QUESTION, id="q2")) != base
    assert fp() == base                       # and it is a function


def test_question_digest_is_content():
    """Content, not id — so a question cannot be excluded from one dataset
    by an unrelated dataset that happened to reuse the id, and a dataset
    rebuilt with renumbered ids still matches itself."""
    base = reuse.question_digest(QUESTION)
    assert reuse.question_digest(dict(QUESTION)) == base
    assert reuse.question_digest(dict(QUESTION, id="other")) != base
    assert reuse.question_digest(dict(QUESTION, answer="Fire")) != base


def test_content_not_formatting(tmp_path):
    """A syntax file reindented is the same experiment; a field changed is
    not. Hashing the bytes would re-run a campaign over a stray newline."""
    a = tmp_path / "a.json"
    b = tmp_path / "b.json"
    c = tmp_path / "c.json"
    a.write_text('{"prompt_indent": "  ", "desc_inline": true}')
    b.write_text('{\n  "desc_inline": true,\n  "prompt_indent": "  "\n}\n')
    c.write_text('{"prompt_indent": "\\t", "desc_inline": true}')
    assert reuse.json_digest(a) == reuse.json_digest(b)
    assert reuse.json_digest(a) != reuse.json_digest(c)


def test_program_hash_is_the_artifact(repo_root):
    """The STA's own hash, so the digest covers the transitive imports and
    the compile options — not the .stl path, which says nothing about
    what the file contained."""
    import autocog

    demos = os.path.join(repo_root, "share", "demos", "mcq")
    stlib = os.path.join(repo_root, "share", "library", "stlib")

    rel = reuse.program_hash(autocog.compile(
        os.path.join("share", "demos", "mcq", "record-index.stl"),
        includes=[stlib]))
    absolute = reuse.program_hash(autocog.compile(
        os.path.join(demos, "record-index.stl"), includes=[stlib]))
    other = reuse.program_hash(autocog.compile(
        os.path.join(demos, "record-value.stl"), includes=[stlib]))

    # Stable across where it was compiled from: a campaign run from a
    # different working directory must still match its own prior results.
    assert rel == absolute
    assert rel != other


# --- what an earlier campaign used --------------------------------------

def write(path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    if str(path).endswith(".ndjson"):
        path.write_text("".join(json.dumps(r) + "\n" for r in obj))
    else:
        path.write_text(json.dumps(obj))


def test_prior_reads_selections_and_rows(tmp_path):
    """Two sources, failing differently. The selection file is what the
    campaign committed to, and covers arms that never ran; the result rows
    are the backstop for anything produced outside a campaign.

    Both are scanned recursively: a digest says nothing about which run
    directory it came from, which is what lets an earlier campaign with a
    different layout be subtracted at all."""
    q1 = dict(QUESTION, id="a")
    q2 = dict(QUESTION, id="b", question="What is CO2?")
    write(tmp_path / "old" / "samples-mmlu.json", [q1])
    write(tmp_path / "old" / "arm" / "deep" / "results-h-rng.ndjson",
          [{"autocog.bench.question.digest": reuse.question_digest(q2)}])

    seen = reuse.prior_questions([str(tmp_path / "old")])
    assert seen == {reuse.question_digest(q1), reuse.question_digest(q2)}


def test_a_failed_sample_still_spent_its_question(tmp_path):
    """Unlike a resume, an error row COUNTS here. A resume re-runs a failed
    sample because it is not a measurement; a later campaign must still
    avoid that question, because drawing it again is an intersection."""
    write(tmp_path / "old" / "results-h-rng.ndjson", [
        {"autocog.bench.question.digest": "spent",
         "error.message": "RemoteError: worker died"}])
    assert reuse.prior_questions([str(tmp_path / "old")]) == {"spent"}


def test_torn_lines_are_tolerated(tmp_path):
    write(tmp_path / "old" / "results-h-rng.ndjson",
          [{"autocog.bench.question.digest": "good"}])
    with open(tmp_path / "old" / "results-h-rng.ndjson", "a") as f:
        f.write('{"autocog.bench.question.dig')          # killed mid-write
    assert reuse.prior_questions([str(tmp_path / "old")]) == {"good"}


# --- drawing a campaign's questions --------------------------------------

def test_exclude_draws_the_full_count_from_what_is_left(tmp_path):
    """The point of excluding BEFORE the draw: the second campaign gets its
    full N of fresh questions, not N minus however many collided."""
    from autocog.bench.formatters import load_questions

    pool = [dict(QUESTION, id=f"q{i}", question=f"Q{i}?") for i in range(20)]
    data = tmp_path / "pool.json"
    write(data, pool)

    first = load_questions(str(data), limit=5)
    assert len(first) == 5

    used = {reuse.question_digest(q) for q in first}
    second = load_questions(str(data), limit=5, exclude=used)
    assert len(second) == 5                                   # full count
    assert not used & {reuse.question_digest(q) for q in second}   # disjoint

    third = load_questions(str(data), limit=5,
                           exclude=used | {reuse.question_digest(q)
                                           for q in second})
    assert len(third) == 5
    assert len({q["id"] for q in first + second + third}) == 15


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


# --- end to end, through a run -------------------------------------------

SHARED = (("share", "syntax", "complete.json"),
          ("share", "search", "default.json"),
          ("share", "benchmarks", "quality", "questions.json"))


def fake_root(repo_root, dst):
    """A repo root whose syntax and search are ours to edit — the only way
    to test "the configuration changed" without editing the checkout."""
    for parts in SHARED:
        os.makedirs(os.path.join(dst, *parts[:-1]), exist_ok=True)
        shutil.copy(os.path.join(repo_root, *parts),
                    os.path.join(dst, *parts))
    return dst


@pytest.fixture(scope="module")
def rng_worker():
    """An in-process backend serving only the rng pseudo-model: enough to
    exercise the pooled path, which is the one campaigns take, without a
    GGUF and without a subprocess."""
    from autocog.server import backend as backend_srv
    from test_bench_remote import running_server

    with running_server(backend_srv.create_app(models=[])) as port:
        yield [f"127.0.0.1:{port}"]


def quality(out, root, log=None, workers=None):
    from autocog.bench.quality import run_quality

    return run_quality(
        questions=2, syntaxes=["complete"], root=str(root),
        data=os.path.join(root, *SHARED[-1]), workers=workers,
        programs=["share/demos/mcq/strings-select.stl"],
        out=str(out), log=log or (lambda *_: None))


def test_every_row_carries_both_identities(tmp_path, repo_root, rng_worker):
    """The fingerprint for this run's own resume, the question digest for
    the next campaign's draw. A row missing either is data that cannot be
    accounted for later."""
    root = fake_root(repo_root, str(tmp_path / "root"))
    for workers in (rng_worker, None):           # pooled and in-process
        evs = quality(tmp_path / str(bool(workers)), root, workers=workers)
        assert len(evs) == 2
        for ev in evs:
            assert ev["autocog.bench.fingerprint"]
            assert ev["autocog.bench.question.digest"]
        assert len({e["autocog.bench.question.digest"] for e in evs}) == 2


def test_resume_rejects_rows_from_a_different_configuration(tmp_path, repo_root,
                                                            rng_worker):
    """Editing a syntax and re-running used to keep the old numbers: the
    (syntax, program, question) triple still matched, so resume called the
    sample done. The triple names a CELL; the fingerprint names the
    measurement."""
    root = fake_root(repo_root, str(tmp_path / "root"))
    out = tmp_path / "one"
    quality(out, root, workers=rng_worker)

    syntax = os.path.join(root, "share", "syntax", "complete.json")
    cfg = json.load(open(syntax))
    cfg["prompt_with_index"] = not cfg.get("prompt_with_index", True)
    json.dump(cfg, open(syntax, "w"))

    logs = []
    quality(out, root, log=logs.append, workers=rng_worker)
    assert any("different configuration" in m for m in logs)

    rows = [json.loads(l) for f in out.glob("results-*.ndjson")
            for l in open(f)]
    assert len(rows) == 4                       # re-measured, not skipped
    assert len({e["autocog.bench.fingerprint"] for e in rows}) == 4


def test_unfingerprinted_rows_still_count_as_done(tmp_path, repo_root):
    """Results written before fingerprints existed keep resuming on the
    triple. Invalidating them would re-run every campaign already on disk
    to buy a guarantee about data that is already collected."""
    from autocog.bench.formatters import load_questions
    from autocog.bench.quality import QualityRun

    root = fake_root(repo_root, str(tmp_path / "root"))
    data = os.path.join(root, *SHARED[-1])
    drawn = [q["id"] for q in load_questions(data, limit=2)]

    out = tmp_path / "legacy"
    out.mkdir()
    write(out / "results-old-rng.ndjson",
          [{"autocog.bench.syntax": "complete",
            "autocog.bench.program": "strings-select",
            "autocog.bench.question": q,
            "autocog.bench.answer": "Water"} for q in drawn])

    run = QualityRun("rng", data, str(out), str(root), questions=2,
                     syntaxes=["complete"],
                     programs=["share/demos/mcq/strings-select.stl"],
                     log=lambda *_: None)
    assert [q["id"] for q in run.qs] == drawn
    assert run.pending == []


# --- through the launcher ------------------------------------------------

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


# --- groups, and syntaxes as base + overrides ----------------------------

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
    from campaign.layout import Layout
    from campaign.syntaxes import materialize_all

    d = descriptor_from(tmp_path, dict(
        BASE, runs=[{"name": "r", "model": "rng", "dataset": "d",
                     "programs": ["a"], "syntaxes": ["complete"]}]))
    assert d.syntax("complete") == {}

    paths, _ = materialize_all(d, Layout(str(repo_root), str(tmp_path)),
                            str(tmp_path / "res"), log=lambda *_: None)
    shipped = json.load(open(os.path.join(repo_root, "share", "syntax",
                                          "complete.json")))
    assert json.load(open(paths["complete"])) == shipped


def test_overrides_apply_to_the_base(tmp_path, repo_root):
    from campaign.layout import Layout
    from campaign.syntaxes import materialize_all

    d = descriptor_from(tmp_path, dict(
        BASE,
        syntaxes={"i1-tab": {"filepath": "complete.json",
                             "prompt_zero_index": False,
                             "prompt_indent": "\t"}},
        runs=[{"name": "r", "model": "rng", "dataset": "d",
               "programs": ["a"], "syntaxes": ["i1-tab"]}]))
    paths, _ = materialize_all(d, Layout(str(repo_root), str(tmp_path)),
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
    from campaign.layout import Layout
    from campaign.syntaxes import materialize_all

    (tmp_path / "complete.json").write_text('{"prompt_indent": "MINE"}')
    d = descriptor_from(tmp_path, dict(
        BASE, runs=[{"name": "r", "model": "rng", "dataset": "d",
                     "programs": ["a"], "syntaxes": ["complete"]}]))
    paths, _ = materialize_all(d, Layout(str(repo_root), str(tmp_path)),
                            str(tmp_path / "res"), log=lambda *_: None)
    assert json.load(open(paths["complete"])) == {"prompt_indent": "MINE"}


def test_an_override_of_a_field_the_base_lacks_is_fatal(tmp_path, repo_root):
    """A typo in a knob name is otherwise a no-op that looks like an arm:
    the campaign runs, two cells render identically, and the contrast the
    descriptor was written for never happened."""
    from campaign.layout import Layout
    from campaign.syntaxes import SyntaxError_, materialize_all

    d = descriptor_from(tmp_path, dict(
        BASE,
        syntaxes={"oops": {"filepath": "complete.json",
                           "prompt_with_indices": True}},   # not a field
        runs=[{"name": "r", "model": "rng", "dataset": "d",
               "programs": ["a"], "syntaxes": ["oops"]}]))
    with pytest.raises(SyntaxError_, match="prompt_with_indices"):
        materialize_all(d, Layout(str(repo_root), str(tmp_path)),
                        str(tmp_path / "res"), log=lambda *_: None)


def test_a_syntax_name_is_not_a_path(tmp_path, repo_root):
    from campaign.layout import Layout
    from campaign.syntaxes import SyntaxError_, materialize_all

    d = descriptor_from(tmp_path, dict(
        BASE, runs=[{"name": "r", "model": "rng", "dataset": "d",
                     "programs": ["a"], "syntaxes": ["grid/i0-typed"]}]))
    with pytest.raises(SyntaxError_, match="a name is a name, not a path"):
        materialize_all(d, Layout(str(repo_root), str(tmp_path)),
                        str(tmp_path / "res"), log=lambda *_: None)


def test_a_run_uses_the_resolved_syntax_not_a_shipped_name(tmp_path, repo_root,
                                                           rng_worker):
    """End to end: the name is the event's grouping key, and the file it
    denotes is the campaign's own resolved JSON — not share/syntax/<name>."""
    from campaign.layout import Layout
    from campaign.syntaxes import materialize_all
    from autocog.bench.quality import run_quality

    d = descriptor_from(tmp_path, dict(
        BASE,
        syntaxes={"no-index": {"filepath": "complete.json",
                               "prompt_with_index": False}},
        runs=[{"name": "r", "model": "rng", "dataset": "d",
               "programs": ["a"], "syntaxes": ["no-index"]}]))
    paths, _ = materialize_all(d, Layout(str(repo_root), str(tmp_path)),
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
    from campaign.layout import Layout
    from campaign.syntaxes import materialize_all

    d = descriptor_from(tmp_path, dict(
        BASE,
        syntaxes={"i1-tab": {"filepath": "complete.json",
                             "labels": ["one-based", "indent-tab"],
                             "prompt_zero_index": False,
                             "prompt_indent": "\t"}},
        runs=[{"name": "r", "model": "rng", "dataset": "d",
               "programs": ["a"], "syntaxes": ["i1-tab"]}]))
    paths, labels = materialize_all(d, Layout(str(repo_root), str(tmp_path)),
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


def test_syntax_labels_reach_the_event_in_their_own_key(tmp_path, repo_root,
                                                        rng_worker):
    """Never merged into the program's labels: `index` is already a program
    label on record-index and is the obvious name for the index knob, so
    one list would make grouping by it mix a mechanism with a rendering."""
    from campaign.layout import Layout
    from campaign.syntaxes import materialize_all
    from autocog.bench.quality import run_quality

    d = descriptor_from(tmp_path, dict(
        BASE,
        syntaxes={"no-index": {"filepath": "complete.json",
                               "labels": ["unindexed"],
                               "prompt_with_index": False}},
        runs=[{"name": "r", "model": "rng", "dataset": "d",
               "programs": ["a"], "syntaxes": ["no-index"]}]))
    paths, labels = materialize_all(d, Layout(str(repo_root), str(tmp_path)),
                                    str(tmp_path / "res"), log=lambda *_: None)

    for workers in (rng_worker, None):
        evs = run_quality(
            questions=1, syntaxes=["no-index"], syntax_paths=paths,
            syntax_labels=labels, root=str(repo_root), workers=workers,
            data=os.path.join(repo_root, "share", "benchmarks", "quality",
                              "questions.json"),
            programs={"record-index": {
                "filepath": os.path.join(repo_root, "share", "demos", "mcq",
                                         "record-index.stl"),
                "labels": ["record", "index"]}},
            out=str(tmp_path / f"out{bool(workers)}"), log=lambda *_: None)
        ev = evs[0]
        assert ev["autocog.bench.labels"] == ["record", "index"]
        assert ev["autocog.bench.syntax.labels"] == [
            "unindexed", "prompt_with_index=false"]


# --- how the draw is spread -----------------------------------------------

def test_the_draw_covers_every_topic(tmp_path):
    """Round-robin over `topic`, not a stride.

    A stride is systematic sampling, and over a subject-sorted frame any
    topic smaller than the step contributes 0 or 1 — so at N=100 over a
    1000-item pool a subject holding seven of them vanished, and which
    subjects vanished was decided by filename order."""
    from autocog.bench.formatters import load_questions

    pool = [dict(QUESTION, id=f"{t}-{i}", question=f"{t}{i}?", topic=t)
            for t, n in (("big", 50), ("mid", 10), ("tiny", 3))
            for i in range(n)]
    data = tmp_path / "pool.json"
    write(data, pool)

    drawn = load_questions(str(data), limit=9)
    assert len(drawn) == 9
    assert {q["topic"] for q in drawn} == {"big", "mid", "tiny"}
    # Equal allocation: MMLU subjects span an order of magnitude, and
    # proportional would give this draw 7 'big' and none of 'tiny'.
    assert [q["topic"] for q in drawn].count("big") == 3

    # Draw order, so the gate taking the first four still spans topics.
    assert {q["topic"] for q in drawn[:3]} == {"big", "mid", "tiny"}


def test_a_topic_running_out_does_not_stall_the_draw(tmp_path):
    from autocog.bench.formatters import load_questions

    pool = [dict(QUESTION, id=f"{t}-{i}", question=f"{t}{i}?", topic=t)
            for t, n in (("big", 20), ("tiny", 1)) for i in range(n)]
    data = tmp_path / "pool.json"
    write(data, pool)
    drawn = load_questions(str(data), limit=10)
    assert len(drawn) == 10
    assert [q["topic"] for q in drawn].count("tiny") == 1


def test_the_next_campaign_continues_down_each_topic(tmp_path):
    """The property `exclude` needs and a stride could not give: removing
    one campaign's draw leaves each topic's remainder in order, so the next
    campaign is drawn the same way rather than on a different composition."""
    from autocog.bench.formatters import load_questions

    pool = [dict(QUESTION, id=f"{t}-{i}", question=f"{t}{i}?", topic=t)
            for t in ("a", "b", "c") for i in range(10)]
    data = tmp_path / "pool.json"
    write(data, pool)

    first = load_questions(str(data), limit=6)
    used = {reuse.question_digest(q) for q in first}
    second = load_questions(str(data), limit=6, exclude=used)

    for drawn in (first, second):
        assert sorted(collections.Counter(
            q["topic"] for q in drawn).values()) == [2, 2, 2]
    assert not used & {reuse.question_digest(q) for q in second}
    assert {q["id"] for q in first} == {"a-0", "b-0", "c-0", "a-1", "b-1", "c-1"}
    assert {q["id"] for q in second} == {"a-2", "b-2", "c-2", "a-3", "b-3", "c-3"}
