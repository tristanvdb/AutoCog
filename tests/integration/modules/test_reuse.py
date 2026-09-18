"""Reusing a sample a previous campaign already paid for.

Two properties, and the second is the one worth the tests: a sample that
IS the same is not re-measured, and a sample that merely looks the same
is. The second is what stops reuse from quietly welding two
configurations into one dataset — the failure that makes results
unusable only after the money is gone.
"""

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


def write_rows(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(json.dumps(r) + "\n" for r in rows))


def test_index_skips_what_is_not_a_measurement(tmp_path):
    """Three kinds of row must not be adoptable: a failure, a row from
    before fingerprints existed, and a half-written last line."""
    write_rows(tmp_path / "run" / "results-h-rng.ndjson", [
        {"autocog.bench.fingerprint": "good", "autocog.bench.answer": "Water"},
        {"autocog.bench.fingerprint": "dead", "error.message": "worker died"},
        {"autocog.bench.answer": "Water"},                       # no fingerprint
    ])
    with open(tmp_path / "run" / "results-h-rng.ndjson", "a") as f:
        f.write('{"autocog.bench.fingerprint": "tor')             # killed mid-write

    index = reuse.PriorIndex([str(tmp_path)])
    assert "good" in index and len(index) == 1
    assert "dead" not in index          # a dead worker is not a datapoint
    assert index.get("tor") is None


def test_index_is_layout_independent_and_supersedes(tmp_path):
    """The fingerprint is complete, so which campaign, phase or run
    directory a row sits in does not matter — which is the whole reason a
    prior campaign with different run labels can be reused at all.

    And a re-measured sample supersedes: scanning is sorted, last wins."""
    write_rows(tmp_path / "old" / "arm-a" / "results-h-rng.ndjson",
               [{"autocog.bench.fingerprint": "x", "autocog.bench.answer": "stale"}])
    write_rows(tmp_path / "old" / "arm-b" / "deep" / "results-h-rng.ndjson",
               [{"autocog.bench.fingerprint": "x", "autocog.bench.answer": "fresh"},
                {"autocog.bench.fingerprint": "y", "autocog.bench.answer": "Fire"}])

    index = reuse.PriorIndex([str(tmp_path / "old")])
    assert len(index) == 2
    assert index.get("x")["autocog.bench.answer"] == "fresh"


def test_adopted_row_keeps_when_it_was_measured(tmp_path):
    """The measurement's @timestamp is when it was MEASURED. Restamping it
    would erase the only evidence that the row was not produced by the run
    it now sits in."""
    write_rows(tmp_path / "r" / "results-h-rng.ndjson", [
        {"autocog.bench.fingerprint": "x", "@timestamp": "2026-01-01T00:00:00Z",
         "autocog.bench.answer": "Water"}])
    ev = reuse.PriorIndex([str(tmp_path)]).get("x")
    assert ev["@timestamp"] == "2026-01-01T00:00:00Z"
    assert ev["autocog.bench.reused.at"] == "2026-01-01T00:00:00Z"
    assert ev["autocog.bench.reused.from"].endswith("results-h-rng.ndjson")


# --- end to end, through a run ------------------------------------------

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


def quality(out, root, prior=None, log=None, workers=None):
    from autocog.bench.quality import run_quality

    return run_quality(
        questions=2, syntaxes=["complete"], root=str(root),
        data=os.path.join(root, *SHARED[-1]), workers=workers,
        programs=["share/demos/mcq/strings-select.stl"],
        out=str(out), prior=prior, log=log or (lambda *_: None))


def test_second_campaign_adopts_and_stays_self_contained(tmp_path, repo_root,
                                                        rng_worker):
    """The ask: run the same campaign twice, pay once.

    The adopted rows are COPIED into the second run's directory rather
    than referenced. A campaign that ships as a tarball of pointers into a
    directory somebody has since archived is a campaign that cannot be
    read, and that is the expensive way to find out."""
    root = fake_root(repo_root, str(tmp_path / "root"))
    first = quality(tmp_path / "one", root, workers=rng_worker)
    assert len(first) == 2
    assert all(e["autocog.bench.fingerprint"] for e in first)

    logs = []
    second = quality(tmp_path / "two", root, prior=[str(tmp_path / "one")],
                     log=logs.append, workers=rng_worker)
    assert len(second) == 2
    assert any("reused 2 sample(s)" in m for m in logs)
    # Not "ran quickly" — nothing was scheduled at all.
    assert any("nothing to do" in m for m in logs)

    lines = [json.loads(l) for l in
             open(next((tmp_path / "two").glob("results-*.ndjson")))]
    assert len(lines) == 2
    assert {e["autocog.bench.fingerprint"] for e in lines} == \
           {e["autocog.bench.fingerprint"] for e in first}
    for e in lines:
        assert "autocog.bench.reused.from" in e
        assert e["autocog.bench.answer"] is not None

    # And the summary is regenerated over them: a run that only adopted
    # still has to look like a finished run.
    assert list((tmp_path / "two").glob("results-*.md"))


def test_edited_syntax_is_not_the_same_experiment(tmp_path, repo_root,
                                                 rng_worker):
    """Same model, same program, same question, same cell NAME — and a
    different rendering. Reusing here is how two configurations end up
    averaged together in one table, so it must re-measure."""
    root = fake_root(repo_root, str(tmp_path / "root"))
    quality(tmp_path / "one", root, workers=rng_worker)

    syntax = os.path.join(root, "share", "syntax", "complete.json")
    cfg = json.load(open(syntax))
    cfg["prompt_indent"] = "> " if cfg.get("prompt_indent") != "> " else "  "
    json.dump(cfg, open(syntax, "w"))

    logs = []
    second = quality(tmp_path / "two", root, prior=[str(tmp_path / "one")],
                     log=logs.append, workers=rng_worker)
    assert len(second) == 2
    assert not any("reused" in m for m in logs)
    assert all("autocog.bench.reused.from" not in e for e in second)


def test_resume_rejects_rows_from_a_different_configuration(tmp_path, repo_root,
                                                           rng_worker):
    """The same guard on the run's OWN directory. Editing a syntax and
    re-running used to keep the old numbers: the triple still matched, so
    resume called the sample done."""
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


def test_local_path_reuses_but_does_not_resume(tmp_path, repo_root):
    """The in-process path stamps identity and honours --prior, so an
    ad-hoc run is reusable data and can itself reuse.

    It does NOT resume on its own directory: a campaign is a matrix you
    resume into, but `bench quality` re-run by hand and quietly measuring
    nothing would be a trap. Asking for reuse is explicit."""
    root = fake_root(repo_root, str(tmp_path / "root"))
    first = quality(tmp_path / "one", root)
    assert len(first) == 2
    assert all(e["autocog.bench.fingerprint"] for e in first)

    logs = []
    again = quality(tmp_path / "one", root, log=logs.append)
    assert len(again) == 2                              # no implicit resume
    assert not any("reused" in m for m in logs)

    logs = []
    second = quality(tmp_path / "two", root, prior=[str(tmp_path / "one")],
                     log=logs.append)
    assert any("reused 2 sample(s)" in m for m in logs)
    assert all("autocog.bench.reused.from" in e for e in second)


# --- through a campaign -------------------------------------------------

def campaign_desc(path, name, prior=None):
    desc = {
        "name": name,
        "phases": [{"name": "p", "runs": [
            {"kind": "quality", "model": "rng",
             "data": "share/benchmarks/quality/questions.json",
             "questions": 2, "syntaxes": ["complete"],
             "programs": ["share/demos/mcq/strings-select.stl"],
             "out": f"arm-{name}"}]}],
    }
    if prior:
        desc["prior"] = prior
    path.write_text(json.dumps(desc))
    return str(path)


def test_a_second_campaign_pays_only_for_what_is_new(tmp_path, monkeypatch,
                                                     rng_worker):
    """Different campaign NAME, different run label, different output
    directory — and the same samples, so nothing is measured twice.

    Per-run resume could never do this: it only ever looks in the run's own
    directory, so an archived campaign is a campaign you pay for again."""
    from autocog.bench.campaign import run_campaign

    monkeypatch.setenv("RESULTS_PATH", str(tmp_path / "results"))
    run_campaign(campaign_desc(tmp_path / "one.json", "one"),
                 workers=rng_worker, log=lambda *_: None)

    logs = []
    root, failures = run_campaign(
        campaign_desc(tmp_path / "two.json", "two", prior=["one"]),
        workers=rng_worker, log=logs.append)
    assert failures == []
    assert any("reused 2 sample(s)" in m for m in logs)
    assert any("skipping" in m for m in logs)

    rows = [json.loads(l)
            for f in (tmp_path / "results" / "two").rglob("results-*.ndjson")
            for l in open(f)]
    assert len(rows) == 2
    assert all("autocog.bench.reused.from" in e for e in rows)


def test_a_prior_that_does_not_exist_is_fatal(tmp_path, monkeypatch, rng_worker):
    """Not an empty index. Reuse exists to not spend money twice, so a typo
    that degrades into "reuse nothing" fails the one way the operator
    cannot see: the campaign just runs, and costs what it should save."""
    from autocog.bench.campaign import run_campaign
    from autocog.errors import ConfigError

    monkeypatch.setenv("RESULTS_PATH", str(tmp_path / "results"))
    with pytest.raises(ConfigError, match="not found"):
        run_campaign(campaign_desc(tmp_path / "c.json", "c", prior=["typo"]),
                     workers=rng_worker, log=lambda *_: None)


def test_launcher_resolves_prior_by_name_or_path(tmp_path, repo_root):
    """The app's own resolution: a campaign name under results/, or a path."""
    import sys

    sys.path.insert(0, os.path.join(repo_root, "share"))
    from campaign.layout import Layout
    from campaign.runner import resolve_prior

    layout = Layout(repo_root, str(tmp_path))
    os.makedirs(os.path.join(tmp_path, "results", "earlier"))
    elsewhere = tmp_path / "elsewhere"
    elsewhere.mkdir()

    assert resolve_prior(layout, ["earlier", str(elsewhere)]) == [
        os.path.join(str(tmp_path), "results", "earlier"), str(elsewhere)]
    with pytest.raises(FileNotFoundError, match="typo"):
        resolve_prior(layout, ["typo"])


def test_unfingerprinted_rows_still_count_as_done(tmp_path, repo_root):
    """Results written before fingerprints existed keep resuming on the
    triple. Invalidating them would re-run every campaign already on disk
    to buy a guarantee about data that is already collected."""
    root = fake_root(repo_root, str(tmp_path / "root"))
    out = tmp_path / "legacy"
    out.mkdir()
    rows = []
    for q in ("water", "twelve"):
        rows.append({"autocog.bench.syntax": "complete",
                     "autocog.bench.program": "strings-select",
                     "autocog.bench.question": q,
                     "autocog.bench.answer": "Water"})
    write_rows(out / "results-old-rng.ndjson", rows)

    from autocog.bench.quality import QualityRun

    run = QualityRun("rng", os.path.join(root, *SHARED[-1]), str(out),
                     str(root), questions=2, syntaxes=["complete"],
                     programs=["share/demos/mcq/strings-select.stl"],
                     log=lambda *_: None)
    assert [q["id"] for q in run.qs] == ["water", "twelve"]
    assert run.pending == []
