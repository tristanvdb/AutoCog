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


def test_campaign(tmp_path):
    cells = tmp_path / "cells.json"
    cells.write_text(json.dumps([{"beams": 1, "ahead": 1, "width": 1}]))
    manifest = tmp_path / "campaign.json"
    manifest.write_text(json.dumps({
        "name": "smoke",
        "out": str(tmp_path / "results"),
        "runs": [
            {"kind": "perf", "cells": str(cells), "tag": "p"},
            {"kind": "quality", "questions": 1,
             "syntaxes": ["complete"], "demos": ["select"], "out": "q"},
            {"kind": "quality", "data": "does-not-exist.json", "out": "broken"},
        ],
    }))
    logs = []
    out = run_campaign(str(manifest), log=logs.append)
    assert os.path.isdir(out)
    assert any(f.startswith("results-")                               # perf run
               for f in os.listdir(os.path.join(out, "p")))
    assert os.path.isdir(os.path.join(out, "q"))                      # quality run
    assert any("[failed] 1 run" in l for l in logs)                   # isolation
