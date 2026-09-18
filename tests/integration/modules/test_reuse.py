"""Sample identity, at its two granularities.

The SAMPLE fingerprint decides whether the rows in a run's own output
describe the experiment it is about to run. The QUESTION digest decides
which datapoints a campaign draws — subtracting what an earlier campaign
already used, so their sample sets do not intersect and their results can
be unioned without double-counting anything.
"""

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
    root = fake_root(repo_root, str(tmp_path / "root"))
    out = tmp_path / "legacy"
    out.mkdir()
    write(out / "results-old-rng.ndjson",
          [{"autocog.bench.syntax": "complete",
            "autocog.bench.program": "strings-select",
            "autocog.bench.question": q,
            "autocog.bench.answer": "Water"} for q in ("water", "twelve")])

    from autocog.bench.quality import QualityRun

    run = QualityRun("rng", os.path.join(root, *SHARED[-1]), str(out),
                     str(root), questions=2, syntaxes=["complete"],
                     programs=["share/demos/mcq/strings-select.stl"],
                     log=lambda *_: None)
    assert [q["id"] for q in run.qs] == ["water", "twelve"]
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
