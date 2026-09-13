"""The multi-GPU campaign harness (benchmarks/campaigns/client.py) on its
GPU-less testing topology: spawn 2 RNG workers, dispatch a 2-run campaign
across them in parallel, verify results and teardown. The harness is pure
orchestration over the package surface (`autocog backend` + run-level
`workers`), so this is also an end-to-end check of that surface.
"""

import json
import os
import subprocess
import sys
import urllib.error
import urllib.request

import pytest


@pytest.mark.timeout(240)
def test_client_rng_topology(tmp_path, repo_root):
    client = repo_root / "benchmarks" / "campaigns" / "client.py"
    data = repo_root / "benchmarks" / "quality" / "questions.json"
    manifest = {
        "name": "harness-test",
        "out": str(tmp_path / "results"),
        "runs": [
            {"kind": "quality", "data": str(data), "questions": 2,
             "syntaxes": ["complete"], "demos": ["select"], "out": "sel"},
            {"kind": "quality", "data": str(data), "questions": 2,
             "syntaxes": ["complete"], "demos": ["repeat"], "out": "rep"},
        ],
    }
    mpath = tmp_path / "manifest.json"
    mpath.write_text(json.dumps(manifest))

    r = subprocess.run(
        [sys.executable, str(client), str(mpath), "--rng", "2",
         "--ready-timeout", "60"],
        capture_output=True, text=True, cwd=str(repo_root), timeout=220)
    assert r.returncode == 0, r.stdout[-2000:] + r.stderr[-1000:]

    # Both runs dispatched, to two distinct workers (parallel topology).
    assert "[sel] done" in r.stdout and "[rep] done" in r.stdout
    targets = {line.split("@")[-1].strip() for line in r.stdout.splitlines()
               if "] -> worker-" in line}
    assert len(targets) == 2, r.stdout

    # Results landed per run; every event is an rng datapoint.
    for sub in ("sel", "rep"):
        nd = [f for f in os.listdir(tmp_path / "results" / sub)
              if f.endswith(".ndjson")]
        assert nd, f"no results for {sub}"
        events = [json.loads(l) for l in
                  (tmp_path / "results" / sub / nd[0]).read_text().splitlines()]
        assert len(events) == 2
        assert all(e["autocog.bench.model"] == "rng" for e in events)

    # Worker logs exist; workers are gone (their ports refuse).
    logs = os.listdir(tmp_path / "results" / "workers")
    assert sorted(logs) == ["worker-0.log", "worker-1.log"]
    up = [line for line in r.stdout.splitlines() if line.startswith("[up]")]
    assert len(up) == 2
    for line in up:
        url = line.split("@")[1].split()[0].strip()
        with pytest.raises((urllib.error.URLError, OSError)):
            urllib.request.urlopen(f"http://{url}/capabilities", timeout=3)

    # No mini-manifest droppings next to the manifest.
    assert not [f for f in os.listdir(tmp_path) if f.startswith(".client-")]


@pytest.mark.timeout(180)
def test_client_sanity_gate(tmp_path, repo_root):
    """--sanity-only: per-worker probes (affinity echo + one-question run)
    pass on the rng topology and the client exits before dispatching."""
    client = repo_root / "benchmarks" / "campaigns" / "client.py"
    manifest = {"name": "sanity-test", "out": str(tmp_path / "results"),
                "runs": [{"kind": "quality", "questions": 1,
                          "syntaxes": ["complete"], "demos": ["select"],
                          "out": "never-dispatched"}]}
    mpath = tmp_path / "manifest.json"
    mpath.write_text(json.dumps(manifest))

    r = subprocess.run(
        [sys.executable, str(client), str(mpath), "--rng", "2",
         "--sanity-only", "--ready-timeout", "60"],
        capture_output=True, text=True, cwd=str(repo_root), timeout=160)
    assert r.returncode == 0, r.stdout[-2000:] + r.stderr[-1000:]
    assert r.stdout.count("affinity ok") == 2
    assert r.stdout.count("probe ok") == 2
    assert "[sanity] all workers pass" in r.stdout
    # sanity-only: no dispatch happened
    assert "never-dispatched" not in r.stdout
    assert not (tmp_path / "results" / "never-dispatched").exists()
    assert (tmp_path / "results" / "sanity" / "worker-0").is_dir()


@pytest.mark.timeout(120)
def test_probe_choices_rng(tmp_path, repo_root):
    """The adjudication probe: every candidate branch extracted with its
    forced NLL, all four scoring rules computed offline, select showing
    zero structural length bias (single-token digit candidates)."""
    probe = repo_root / "benchmarks" / "campaigns" / "probe_choices.py"
    data = repo_root / "benchmarks" / "quality" / "questions.json"
    sel = tmp_path / "sel.ndjson"
    rep = tmp_path / "rep.ndjson"
    for demo, out in (("select", sel), ("repeat", rep)):
        r = subprocess.run(
            [sys.executable, str(probe), "run", "--demo", demo,
             "--data", str(data), "--limit", "4", "--out", str(out)],
            capture_output=True, text=True, cwd=str(repo_root), timeout=110)
        assert r.returncode == 0, r.stdout + r.stderr
        rows = [json.loads(l) for l in out.read_text().splitlines()]
        assert len(rows) == 4
        for row in rows:
            idx = sorted(c["i"] for c in row["candidates"])
            assert idx == list(range(len(idx)))          # every candidate
            assert all(c["n_tok"] >= 1 for c in row["candidates"])

    r = subprocess.run(
        [sys.executable, str(probe), "analyze", str(sel), str(rep),
         "--out", str(tmp_path / "report")],
        capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stdout + r.stderr
    report = json.loads((tmp_path / "report.json").read_text())
    groups = {(e["demo"]): e for e in report}
    assert set(groups) == {"select", "repeat"}
    # select candidates are single digits: identical lengths, so every
    # rule scores identically and length bias is structurally zero.
    s = groups["select"]
    assert s["acc.sum"] == s["acc.mean"] == s["acc.bytes"] == s["acc.bayes"]
    assert s["tau.sum"] == 0.0 and s["b_hat"] == 0.0
    for rule in ("sum", "mean", "bytes", "bayes"):
        assert 0.0 <= groups["repeat"][f"acc.{rule}"] <= 1.0


@pytest.mark.timeout(600)
def test_autopilot_smoke_pipeline(repo_root):
    """The unattended campaign driver end to end on rng: all stages green,
    checkpoint state written, rerun skips everything, summary emitted."""
    auto = repo_root / "benchmarks" / "campaigns" / "autopilot.py"
    outdir = repo_root / "benchmarks" / "campaigns" / "results" / "autopilot-smoke"
    import shutil
    shutil.rmtree(outdir.parent, ignore_errors=True)

    r = subprocess.run([sys.executable, str(auto), "--smoke-test"],
                       capture_output=True, text=True, cwd=str(repo_root),
                       timeout=580)
    assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-1000:]

    state = json.loads((outdir / "autopilot-state.json").read_text())
    assert all(rec["status"] == "ok" for rec in state["stages"].values())
    assert set(state["stages"]) >= {"preflight", "w1-smoke", "w1-gate",
                                    "w2-small", "w3-probes", "w3-analysis",
                                    "w5-termination"}
    assert "Failed stages: none" in (outdir / "SUMMARY.md").read_text()
    adjud = outdir.parent / "probe" / "adjudication.json"
    assert adjud.is_file() and json.loads(adjud.read_text())

    # Resume semantics: a second invocation skips every stage.
    r2 = subprocess.run([sys.executable, str(auto), "--smoke-test"],
                        capture_output=True, text=True, cwd=str(repo_root),
                        timeout=120)
    assert r2.returncode == 0
    assert r2.stdout.count("already complete") == len(state["stages"])
    shutil.rmtree(outdir.parent, ignore_errors=True)
