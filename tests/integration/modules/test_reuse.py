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

import pytest

from autocog.bench import reuse


@pytest.fixture(autouse=True)
def from_repo_root(repo_root, monkeypatch):
    monkeypatch.chdir(repo_root)


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

# --- groups, and syntaxes as base + overrides ----------------------------

def test_syntax_labels_reach_the_event_in_their_own_key(tmp_path, repo_root,
                                                        rng_worker):
    """A syntax's labels ride in their own key, never merged into the
    program's: `index` is already a program label on record-index and is
    the obvious name for the index knob, so one list would make grouping
    by it mix a mechanism with a rendering.

    (Where the labels come from is the harness's business — see
    share/experiments/tests.)"""
    from autocog.bench.quality import run_quality

    for workers in (rng_worker, None):
        evs = run_quality(
            questions=1, syntaxes=["complete"], root=str(repo_root),
            syntax_labels={"complete": ["unindexed", "prompt_with_index=false"]},
            workers=workers,
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


# --- __syntax__ bound at a call site --------------------------------------

def syntax_variant(repo_root, tmp_path, **overrides):
    base = json.load(open(os.path.join(repo_root, "share", "syntax",
                                       "complete.json")))
    path = tmp_path / f"syn-{len(overrides)}-{hash(tuple(sorted(overrides.items())))}.json"
    path.write_text(json.dumps(dict(base, **overrides)))
    return str(path)


def test_a_call_may_spread_and_name(repo_root):
    """`call f _ { k use ... }`. The two used to be exclusive in the grammar
    AND the runtime, so a resolver needing one thing the frame does not
    carry had to give up the spread and list every field by hand."""
    import autocog

    prog = autocog.compile(
        os.path.join(repo_root, "share", "demos", "mcq", "record-index.stl"),
        includes=[os.path.join(repo_root, "share", "library", "stlib")])
    call = json.loads(prog.dump_json())["prompts"]["main"]["flows"]["return"] \
        ["fields"][0]["call"]
    assert call["spread"] == []                       # `_`: the whole frame
    assert [k["name"] for k in call["kwargs"]] == ["zero_index"]
    # RECORDED, not evaluated: the syntax is chosen at instantiation, so a
    # compiled program cannot carry the value.
    assert call["kwargs"][0]["system"] == "__syntax__.prompt_zero_index"
    assert call["kwargs"][0]["value"] is None
    assert call["kwargs"][0]["prompt"] is None        # not a prompt reference


def test_the_binding_moves_the_program_hash(repo_root, tmp_path):
    """It is part of the program, so it is part of the program's identity —
    which is what makes `autocog.bench.fingerprint` notice."""
    import autocog
    from autocog.bench import reuse

    stlib = os.path.join(repo_root, "share", "library", "stlib")
    src = open(os.path.join(repo_root, "share", "demos", "mcq",
                            "record-index.stl")).read()
    demos = os.path.join(repo_root, "share", "demos", "mcq")
    plain = tmp_path / "plain.stl"
    plain.write_text(src.replace(
        " { zero_index use __syntax__.prompt_zero_index; }", ""))

    bound = reuse.program_hash(autocog.compile(
        os.path.join(demos, "record-index.stl"), includes=[stlib]))
    unbound = reuse.program_hash(autocog.compile(
        str(plain), includes=[stlib, demos]))
    assert bound != unbound


def test_select_resolves_under_either_index_base(repo_root, tmp_path):
    """THE BUG. `select` emits a POSITION and prompt_zero_index decides
    whether it is spelled from 0 or from 1 — it sets the candidate values,
    not just the `[i]` on a label. Reading the emitted text as a 0-based
    index under a one-indexed syntax names the NEXT choice along, and
    raises only when the model happens to pick the last one."""
    import autocog
    from autocog.externals import load_externals
    from autocog.recorder import Recorder
    import tempfile

    demos = os.path.join(repo_root, "share", "demos", "mcq")
    prog = autocog.compile(os.path.join(demos, "record-index.stl"),
                           includes=[os.path.join(repo_root, "share",
                                                  "library", "stlib")])
    externals = load_externals(prog)
    q = json.load(open(os.path.join(repo_root, *SHARED[-1])))[0]
    search = os.path.join(repo_root, "share", "search", "default.json")

    for zero in (True, False):
        engine = autocog.Engine(
            syntax=syntax_variant(repo_root, tmp_path,
                                  prompt_zero_index=zero),
            search=search)
        with tempfile.TemporaryDirectory() as tmp:
            rec = Recorder(kinds={"frame"}, path=tmp)
            out = engine.run(prog, externals=externals, recorder=rec,
                             topic=q.get("topic", ""), question=q["question"],
                             choice=q["choice"])
            frames = [json.load(open(os.path.join(d, f)))
                      for d, _, fs in os.walk(tmp) for f in fs
                      if f.endswith(".frame.json")]

        # The frame keeps what the model EMITTED, verbatim — that is what
        # makes it re-renderable. The resolver un-spells it.
        emitted = int(frames[0]["answer"])
        meant = q["choice"][emitted - (0 if zero else 1)]["value"]
        assert out == meant, (zero, emitted, out, meant)


def test_an_unknown_syntax_field_is_an_error(repo_root, tmp_path):
    """None flowing into a resolver as if it were a value is how the
    rendering knobs went unnoticed in the first place."""
    import autocog
    from autocog.channels import resolve_system
    from autocog.errors import ConfigError

    engine = autocog.Engine(
        syntax=syntax_variant(repo_root, tmp_path),
        search=os.path.join(repo_root, "share", "search", "default.json"))
    assert resolve_system("__syntax__.prompt_zero_index", engine) in (True, False)
    with pytest.raises(ConfigError, match="no field 'nope'"):
        resolve_system("__syntax__.nope", engine)
    with pytest.raises(ConfigError, match="unsupported system namespace"):
        resolve_system("__status__.tree.tokens", engine)
