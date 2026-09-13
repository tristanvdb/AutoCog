"""Perf cells: the `stop` key carries a termination predicate (TermExpr
JSON object form) through the cell's search config into the FTA, and the
evaluation actually terminates early on rng."""

import json

import pytest


@pytest.fixture(autouse=True)
def from_repo_root(repo_root, monkeypatch):
    monkeypatch.chdir(repo_root)


def test_cell_stop_terminates_early(tmp_path):
    from autocog.bench.perf import run_perf

    cells = tmp_path / "cells.json"
    cells.write_text(json.dumps([
        {"label": "nostop", "beams": 4, "ahead": 2, "width": 2},
        {"label": "stopped", "beams": 4, "ahead": 2, "width": 2,
         "stop": {"ge": ["tokens", 200]}},
    ]))
    events = run_perf(model=None, cells=str(cells), out=str(tmp_path),
                      log=lambda m: None)
    by = {e["autocog.bench.label"]: e for e in events}
    assert by["nostop"]["autocog.perf.search.stopped"] is False
    assert by["stopped"]["autocog.perf.search.stopped"] is True
    assert (by["stopped"]["autocog.perf.search.terminals"]
            < by["nostop"]["autocog.perf.search.terminals"])


def test_cell_stop_with_model_ref(tmp_path):
    """A __model__-referencing guard folds at evaluation setup (rng
    context size) and the cell completes normally."""
    from autocog.bench.perf import run_perf

    cells = tmp_path / "cells.json"
    cells.write_text(json.dumps([
        {"label": "guard", "beams": 2, "ahead": 1, "width": 1,
         "stop": {"ge": ["tokens", {"ref": "model.n_ctx"}]}},
    ]))
    events = run_perf(model=None, cells=str(cells), out=str(tmp_path),
                      log=lambda m: None)
    assert len(events) == 1
    assert events[0]["autocog.perf.search.stopped"] is False  # rng ctx huge
