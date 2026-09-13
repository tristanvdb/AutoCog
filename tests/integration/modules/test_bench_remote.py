"""Remote level-3 workers for autocog bench (--worker host:port).

Gates: bench perf/quality through a worker produce the same integer
counters / answers as the in-process runs; routing picks the worker
hosting the requested model; a spawned worker honors --cpus.
"""

import json
import os
import subprocess
import sys
import time
import urllib.request

import pytest

from autocog.bench.perf import run_perf
from autocog.bench.quality import run_quality
from autocog.bench.workers import RemoteWorker, pick_worker
from autocog.errors import ConfigError

from test_smoke import running_server


@pytest.fixture(autouse=True)
def from_repo_root(repo_root, monkeypatch):
    monkeypatch.chdir(repo_root)


@pytest.fixture
def rng_worker():
    from autocog.server.backend import create_app

    with running_server(create_app(models=[])) as port:
        yield f"localhost:{port}"


PERF_KEYS = ["autocog.perf.tokens.eval", "autocog.perf.tokens.restore",
             "autocog.perf.text.calls", "autocog.perf.complete.calls",
             "autocog.perf.choose.calls", "autocog.perf.search.terminals"]


def test_capabilities(rng_worker):
    caps = RemoteWorker(rng_worker).capabilities()
    assert caps["models"] == ["rng"]
    assert caps["pid"] > 0
    assert isinstance(caps["cpus"], list) and caps["cpus"]


def test_perf_remote_matches_local(rng_worker, tmp_path):
    cells = tmp_path / "cells.json"
    cells.write_text(json.dumps([
        {"beams": 1, "ahead": 1, "width": 1, "label": "one"},
        {"beams": 2, "ahead": 2, "width": 1, "label": "two"},
    ]))
    local = run_perf(cells=str(cells), out=str(tmp_path / "local"),
                     log=lambda *_: None)
    remote = run_perf(cells=str(cells), out=str(tmp_path / "remote"),
                      workers=[rng_worker], log=lambda *_: None)
    assert len(local) == len(remote) == 2
    for l, r in zip(local, remote):
        for key in PERF_KEYS:
            assert l[key] == r[key], key


def test_quality_remote_matches_local(rng_worker, tmp_path):
    kwargs = dict(questions=2, syntaxes=["complete"], demos=["select"],
                  log=lambda *_: None)
    local = run_quality(out=str(tmp_path / "local"), **kwargs)
    remote = run_quality(out=str(tmp_path / "remote"), workers=[rng_worker],
                         **kwargs)
    assert len(local) == len(remote) == 2
    for l, r in zip(local, remote):
        for key in ("autocog.bench.answer", "autocog.bench.correct",
                    "autocog.bench.steps", "autocog.bench.tokens.value",
                    "autocog.bench.tokens.structure",
                    "autocog.bench.friction.value"):
            assert l.get(key) == r.get(key), key


def test_routing(repo_root, rng_worker, tmp_path):
    """Two workers, one hosting the tiny model: jobs route by model tag."""
    model = repo_root / "models" / "tiny-llama3-test-Q2_K.gguf"
    if not model.exists():
        pytest.skip("tiny model not present")
    from autocog.server.backend import create_app

    with running_server(create_app(models=[{"path": str(model)}], n_ctx=2048)) as tiny_port:
        urls = [rng_worker, f"localhost:{tiny_port}"]

        w = pick_worker(urls, str(model))
        assert w.url.endswith(str(tiny_port)) and w.model_tag == "tiny-llama3-test-Q2_K"
        assert pick_worker(urls, None).url.endswith(rng_worker.split(":")[1])

        with pytest.raises(ConfigError, match="no worker hosts"):
            pick_worker(urls, "models/Nonexistent-70B.gguf")

        # A routed evaluation actually engages the real model: decode > 0.
        cells = tmp_path / "cells.json"
        cells.write_text(json.dumps([{"beams": 1, "ahead": 1, "width": 1}]))
        events = run_perf(model=str(model), cells=str(cells),
                          out=str(tmp_path), workers=urls, log=lambda *_: None)
        assert events[0]["autocog.perf.decode.calls"] > 0


def test_spawned_worker_cpu_pinning(tmp_path):
    """`autocog backend --cpus 0-1` in a real subprocess: the worker reports
    the mask through /capabilities."""
    import socket

    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()

    proc = subprocess.Popen(
        [sys.executable, "-m", "autocog", "backend", "--rng",
         "--host", "127.0.0.1", "--port", str(port), "--cpus", "0-1"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.time() + 30
        caps = None
        while time.time() < deadline:
            try:
                with urllib.request.urlopen(
                        f"http://127.0.0.1:{port}/capabilities", timeout=2) as resp:
                    caps = json.loads(resp.read())
                break
            except OSError:
                if proc.poll() is not None:
                    pytest.fail("worker process died during startup")
                time.sleep(0.3)
        assert caps is not None, "worker never came up"
        assert caps["cpus"] == [0, 1]
        assert caps["pid"] == proc.pid
    finally:
        proc.terminate()
        proc.wait(timeout=10)
