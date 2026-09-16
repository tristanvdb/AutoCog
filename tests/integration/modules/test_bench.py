"""autocog bench — in-process drivers (perf / quality / campaign)."""

import json
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


def test_label_mechanism(tmp_path):
    """The `label` answer mechanism: choices render pre-labelled, the model
    emits one letter from the stlib `letter` vocab, and the letter maps back
    to a choice text so accuracy is comparable with select/repeat."""
    from autocog.bench.quality import (LABELS, choices_for, is_label_demo,
                                       score_answer)

    q = {"choices": ["Water", "Fire"], "answer": "Fire"}
    assert is_label_demo("label") and not is_label_demo("select")
    assert choices_for("label", q["choices"]) == ["A. Water", "B. Fire"]
    assert choices_for("select", ["Water"]) == ["Water"]
    # ARC choices arrive pre-labelled by the converter: never double-label
    arc = ["A: parasitism", "B: commensalism", "C: mutualism"]
    assert choices_for("label", arc) == arc
    assert score_answer("label", "C", {"choices": arc,
                                       "answer": "C: mutualism"}) \
        == ("C: mutualism", True)
    assert score_answer("label", "B", q) == ("Fire", True)
    assert score_answer("label", "b\n", q) == ("Fire", True)
    assert score_answer("label", "A", q) == ("Water", False)
    # A letter past the choice count is an unusable label: a MISS, recorded
    # as emitted — never a dropped datapoint.
    assert score_answer("label", "H", q) == ("H", False)
    assert score_answer("label", None, q) == (None, None)
    assert score_answer("select", "Water", q) == ("Water", False)
    assert LABELS[:2] == "AB"

    events = run_quality(questions=3, syntaxes=["complete"], demos=["label"],
                         out=str(tmp_path), log=lambda *_: None)
    assert len(events) == 3
    for ev in events:
        assert ev["autocog.bench.demo"] == "label"
        assert ev.get("error.message") is None
        assert ev["autocog.bench.correct"] in (True, False)   # never dropped


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
                         syntaxes=["complete"], demos=["select"],
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
                    "question": record["q"], "choices": record["opts"],
                    "answer": record["opts"][record["gold"]]}
    """))
    qs = load_questions(str(data), load_formatter(f"{fmt}:to_mcq"))
    assert len(qs) == 1 and qs[0]["answer"] == "Water"

    events = run_quality(data=str(data), formatter=f"{fmt}:to_mcq",
                         out=str(tmp_path), syntaxes=["complete"],
                         demos=["select"], log=lambda *_: None)
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
