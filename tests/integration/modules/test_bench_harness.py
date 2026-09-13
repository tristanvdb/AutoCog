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
