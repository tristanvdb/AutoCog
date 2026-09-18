"""autocog bench — in-process drivers (perf / quality / campaign)."""

import json
import math
import os
import textwrap

import pytest

from autocog.bench.formatters import load_formatter, load_questions
from autocog.bench.perf import run_perf
from autocog.bench.quality import run_quality
from autocog.bench.campaign import run_campaign


@pytest.fixture(autouse=True)
def from_repo_root(repo_root, monkeypatch):
    monkeypatch.chdir(repo_root)


def test_multiprompt_demos(tmp_path):
    """The multi-prompt MCQ programs run end to end: two-stage (hyp),
    retry loops (iter) and mapped per-choice calls (annot, one sub-prompt
    per choice).

    Every frame re-renders, record-index-annot included. It used to be the
    exception — its frame held the resolved record the walker had written
    over the emitted index, and no encoder could match that back to a
    rendered choice. Now the frame holds the index, so there is nothing to
    invert and the friction metrics come out for every demo."""
    for demo, min_steps in (("strings-select-hyp", 2), ("strings-select-iter", 1),
                            ("record-value-annot", 5), ("record-index-annot", 4)):
        events = run_quality(questions=2, syntaxes=["complete"],
                             programs=[f"share/demos/mcq/{demo}.stl"],
                             out=str(tmp_path / demo), log=lambda *_: None)
        assert len(events) == 2, demo
        for ev in events:
            assert ev.get("error.message") is None, (demo, ev.get("error.message"))
            assert ev["autocog.bench.correct"] in (True, False), demo
            assert ev["autocog.bench.steps"] >= min_steps, demo
            assert not ev.get("autocog.bench.score_error"), demo
            # An absent score_error is not the same as a measurement: assert
            # the friction numbers arrived. Scoring silently producing zeros
            # is exactly the failure a "no errors" gate waves through.
            assert ev["autocog.bench.tokens.structure"] > 0, demo
            assert ev["autocog.bench.tokens.value"] > 0, demo
            assert ev["autocog.bench.friction.structure"], demo
            assert ev["autocog.bench.friction.value"], demo


def test_decision_distribution_is_recorded(tmp_path):
    """The candidate distribution at each choice must survive into the event.

    A choose keeps every candidate as a sibling in the FTT -- pruning marks
    them, ranking had to score them all -- so this costs nothing beyond
    writing it down. It is recorded because the question it answers comes
    LATER: at N=100 accuracy is too noisy to say which configurations deserve
    more samples, and "the model was indifferent between all four choices" is
    visible in one evaluation. Aggregating it away would mean re-running to
    ask that question.
    """
    events = run_quality(questions=2, syntaxes=["complete"],
                         programs=["share/demos/mcq/strings-select.stl"],
                         out=str(tmp_path / "dec"), log=lambda *_: None)
    assert len(events) == 2
    for ev in events:
        assert ev.get("error.message") is None
        assert ev["autocog.bench.decision.n"] == 4       # four choices offered
        top1 = ev["autocog.bench.decision.top1"]
        margin = ev["autocog.bench.decision.margin"]
        ent = ev["autocog.bench.decision.entropy"]
        assert 0.25 <= top1 <= 1.0, top1      # 1/n at the floor
        assert 0.0 <= margin <= 1.0, margin
        assert 0.0 <= ent <= math.log(4) + 1e-9, ent
        assert ev["autocog.bench.decision.entropy_norm"] <= 1.0 + 1e-9

        full = ev["autocog.bench.decisions"]
        assert full and isinstance(full, list)
        answer_dec = [d for d in full if d["field"] is not None][-1]
        assert answer_dec["top1"] == top1

        # THE SIGN TRAP. `logprobs` holds NLL, so the model's preferred
        # candidate is the one with the SMALLEST sum. The backend ranked the
        # candidates with its own convention and left exactly one un-pruned;
        # p_chosen is that candidate's probability under ours. If either side
        # inverted, the backend's pick would be our least likely and this
        # fires -- while every other number here would still look plausible.
        assert answer_dec["p_chosen"] == answer_dec["top1"], answer_dec

        # And the chosen candidate is what the model EMITTED, not what the
        # program resolved it to: strings-select commits on an index and
        # `return call pick` turns it into the choice text.
        assert answer_dec["chosen"].isdigit(), answer_dec["chosen"]
        assert ev["autocog.bench.answer"] != answer_dec["chosen"]


def test_perf_rng_cells(tmp_path):
    cells = tmp_path / "cells.json"
    cells.write_text(json.dumps([
        {"beams": 1, "ahead": 1, "width": 1, "label": "one"},
        {"beams": 2, "ahead": 1, "width": 1, "label": "two"},
    ]))
    logs = []
    events = run_perf(cells=str(cells), out=str(tmp_path), tag="t",
                      log=logs.append)
    assert len(events) == 2
    for ev in events:
        assert ev["autocog.perf.tokens.eval"] > 0
        assert "autocog.bench.wall_seconds" in ev
    assert (tmp_path / f"results-{events[0]['host.name']}-rng-t.md").exists()


def test_perf_failed_cell_continues(tmp_path):
    cells = tmp_path / "cells.json"
    cells.write_text(json.dumps([
        {"beams": 1, "ahead": 1, "width": 1, "stl": "no/such.stl", "label": "bad"},
        {"beams": 1, "ahead": 1, "width": 1, "label": "good"},
    ]))
    logs = []
    events = run_perf(cells=str(cells), out=str(tmp_path), log=logs.append)
    assert len(events) == 1
    assert any("FAILED" in l for l in logs)
    assert any("[failed] 1 cell" in l for l in logs)


def test_quality_rng(tmp_path):
    events = run_quality(out=str(tmp_path), questions=2,
                         syntaxes=["complete"],
                         programs=["share/demos/mcq/strings-select.stl"],
                         log=lambda *_: None)
    assert len(events) == 2
    for ev in events:
        assert ev["autocog.bench.answer"] is not None
        assert ev["autocog.bench.tokens.structure"] > 0
        assert ev["autocog.bench.n_choices"] == 4


def test_quality_formatter(tmp_path):
    data = tmp_path / "raw.jsonl"
    data.write_text(
        json.dumps({"q": "What is H2O?", "opts": ["Water", "Fire"], "gold": 0}) + "\n"
        + json.dumps({"q": "skip me", "opts": ["A"], "gold": 0}) + "\n")
    fmt = tmp_path / "fmt.py"
    fmt.write_text(textwrap.dedent("""
        def to_mcq(record):
            if len(record["opts"]) < 2:
                return None
            return {"id": record["q"][:6], "topic": "T",
                    "question": record["q"],
                    "choice": [{"label": l, "value": v}
                               for l, v in zip("ABCDEFGH", record["opts"])],
                    "answer": record["opts"][record["gold"]]}
    """))
    qs = load_questions(str(data), load_formatter(f"{fmt}:to_mcq"))
    assert len(qs) == 1 and qs[0]["answer"] == "Water"

    events = run_quality(data=str(data), formatter=f"{fmt}:to_mcq",
                         out=str(tmp_path), syntaxes=["complete"],
                         programs=["share/demos/mcq/strings-select.stl"],
                         log=lambda *_: None)
    assert len(events) == 1
    assert events[0]["autocog.bench.n_choices"] == 2


def test_campaign(tmp_path, monkeypatch):
    """The executor is a pure worker consumer: no workers = refusal; with
    an rng worker it runs phases, isolates failing runs, and resumes.
    (Full launcher-driven coverage lives in test_campaign.py.)"""
    from autocog.errors import ConfigError
    from autocog.server import backend as backend_srv
    from test_bench_remote import running_server

    cells = tmp_path / "cells.json"
    cells.write_text(json.dumps([{"beams": 1, "ahead": 1, "width": 1}]))
    desc = tmp_path / "campaign.json"
    desc.write_text(json.dumps({
        "name": "smoke",
        "phases": [{"name": "p1", "runs": [
            {"kind": "perf", "cells": str(cells), "tag": "p", "out": "p"},
            {"kind": "quality", "data": "does-not-exist", "out": "broken"},
        ]}],
    }))
    monkeypatch.setenv("RESULTS_PATH", str(tmp_path / "results"))

    with pytest.raises(ConfigError, match="requires --worker"):
        run_campaign(str(desc))

    with running_server(backend_srv.create_app(models=[])) as port:
        logs = []
        out, failures = run_campaign(str(desc),
                                     workers=[f"127.0.0.1:{port}"],
                                     log=logs.append)
        assert any(f.startswith("results-")                           # perf run
                   for f in os.listdir(os.path.join(out, "p")))
        assert failures == ["p1/broken"]                              # isolation
        out2, _ = run_campaign(str(desc), workers=[f"127.0.0.1:{port}"],
                               log=logs.append)
        assert any("skipping" in l for l in logs)                     # resume
