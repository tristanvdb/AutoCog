"""The descriptor campaign stack: multi-model workers, the bench-campaign
executor (pure worker consumer), and the launcher end to end on the
rng+tiny test campaign (tests/fixtures/campaigns/testing.json)."""

import json
import os
import shutil
import socket
import subprocess
import sys
import time
import urllib.request

import pytest


def _free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


TINY = "tiny-llama3-test-Q2_K"


@pytest.fixture(scope="module")
def tiny_gguf(request):
    for base in (os.environ.get("MODELS_PATH"),
                 os.path.join(os.path.dirname(str(request.config.rootpath)),
                              "data", "models"),
                 os.path.join(str(request.config.rootpath), "models")):
        if base and os.path.isfile(os.path.join(base, TINY + ".gguf")):
            return os.path.join(base, TINY + ".gguf")
    pytest.skip("tiny test model not available")


@pytest.fixture()
def worker(tiny_gguf):
    """One backend hosting rng + two tagged instances of the tiny model."""
    port = _free_port()
    spec_a = json.dumps({"path": tiny_gguf, "tag": TINY, "ctx": 1024})
    spec_b = json.dumps({"path": tiny_gguf, "tag": "tiny-again",
                         "ctx": 512, "kv_slots": 8})
    proc = subprocess.Popen(
        [sys.executable, "-m", "autocog", "backend", "--host", "127.0.0.1",
         "--port", str(port), "--model", spec_a, "--model", spec_b],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    url = f"127.0.0.1:{port}"
    try:
        for _ in range(240):
            if proc.poll() is not None:
                pytest.fail(proc.stdout.read().decode()[-2000:])
            try:
                urllib.request.urlopen(f"http://{url}/capabilities", timeout=2)
                break
            except OSError:
                time.sleep(0.5)
        yield url
    finally:
        proc.terminate()
        proc.wait()


def test_multi_model_worker(worker):
    """Two tagged instances with distinct load params + rng behind one URL;
    routing by tag; 404 for unhosted tags."""
    with urllib.request.urlopen(f"http://{worker}/capabilities") as r:
        caps = json.loads(r.read())
    assert set(caps["models"]) == {"rng", TINY, "tiny-again"}
    assert caps["default"] == TINY
    assert caps["lanes"] == 1          # pooled clients bound in-flight by this
    assert caps["details"][TINY]["ctx"] == 1024
    assert caps["details"]["tiny-again"] == {"ctx": 512, "ngl": None,
                                             "kv_slots": 8}

    from autocog.bench.workers import RemoteWorker
    import autocog
    from autocog.runtime.sta import runtime_sta_cxx as rt

    prog = autocog.compile("share/demos/mcq/select.stl",
                           includes=["share/demos/mcq"])
    syn = rt.load_syntax("share/syntax/complete.json")
    sea = rt.load_search("share/search/default.json")

    def eval_on(tag):
        fid = rt.instantiate(prog.id, "main",
                             {"topic": "t", "question": "q",
                              "choices": ["a", "b"]}, syn, sea)
        try:
            return RemoteWorker(worker, model_tag=tag).evaluate_fta(fid)
        finally:
            rt.release_fta(fid)

    assert eval_on("tiny-again")["autocog.perf.tokens.eval"] > 0
    assert eval_on("rng")["autocog.perf.tokens.eval"] > 0
    with pytest.raises(Exception, match="not hosted"):
        eval_on("nope")


@pytest.fixture(autouse=True)
def from_repo_root(repo_root, monkeypatch):
    monkeypatch.chdir(repo_root)


@pytest.fixture()
def rng_workers():
    """Two rng-only backends — the smallest same-tag lane pool."""
    procs, urls = [], []
    try:
        for _ in range(2):
            port = _free_port()
            proc = subprocess.Popen(
                [sys.executable, "-m", "autocog", "backend",
                 "--host", "127.0.0.1", "--port", str(port)],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            procs.append(proc)
            urls.append(f"http://127.0.0.1:{port}")
        for proc, url in zip(procs, urls):
            for _ in range(240):
                if proc.poll() is not None:
                    pytest.fail(proc.stdout.read().decode()[-2000:])
                try:
                    urllib.request.urlopen(f"{url}/capabilities", timeout=2)
                    break
                except OSError:
                    time.sleep(0.5)
        yield urls
    finally:
        for proc in procs:
            proc.terminate()
        for proc in procs:
            proc.wait()


def test_engine_pool_dispatch(rng_workers):
    """Concurrent executions through one EnginePool spread across both
    worker lanes; results stay correct; unhosted tags are rejected."""
    import asyncio

    import autocog
    from autocog.errors import ConfigError
    from autocog.remote import EnginePool

    with pytest.raises(ConfigError, match="does not host"):
        EnginePool(rng_workers, model_tag="nope",
                   syntax="share/syntax/complete.json",
                   search="share/search/default.json")

    pool = EnginePool(rng_workers, model_tag="rng",
                      syntax="share/syntax/complete.json",
                      search="share/search/default.json",
                      poll_interval=0.05)
    assert pool.total_lanes() == 2

    counts = {}
    for b in pool.backends:
        orig = b.evaluate_prompt_async

        async def wrapped(*a, _url=b.server_url, _orig=orig, **kw):
            counts[_url] = counts.get(_url, 0) + 1
            return await _orig(*a, **kw)

        b.evaluate_prompt_async = wrapped

    prog = autocog.compile("share/demos/mcq/select.stl",
                           includes=["share/demos/mcq"])

    async def one(i):
        return await pool.run_async(prog, topic="t", question=f"q{i}",
                                    choices=["a", "b", "c"])

    async def many():
        return await asyncio.gather(*(one(i) for i in range(6)))

    results = asyncio.run(many())
    assert len(results) == 6
    assert all(r in ("a", "b", "c") for r in results)   # select returns the answer
    assert len(counts) == 2 and sum(counts.values()) == 6   # both lanes used


def test_executor_requires_workers(tmp_path):
    from autocog.bench.campaign import run_campaign
    from autocog.errors import ConfigError

    desc = tmp_path / "c.json"
    desc.write_text(json.dumps({"name": "x", "phases": []}))
    with pytest.raises(ConfigError, match="requires --worker"):
        run_campaign(str(desc), workers=None)


def test_executor_refuses_probe_runs(tmp_path, worker, monkeypatch):
    from autocog.bench.campaign import run_campaign

    monkeypatch.setenv("RESULTS_PATH", str(tmp_path))
    desc = tmp_path / "c.json"
    desc.write_text(json.dumps({
        "name": "x",
        "phases": [{"name": "p", "runs": [{"kind": "probe", "model": "rng"}]}]}))
    _, failures = run_campaign(str(desc), workers=[worker], log=lambda m: None)
    assert failures == ["p/p-0"]     # refused, recorded, not fatal


def test_executor_redoes_legacy_partial(tmp_path, worker, monkeypatch):
    """A results file left by a pre-.part writer (interrupted mid-run) is
    detected as incomplete via the expected event count and re-run; once
    complete, resume skips it."""
    from autocog.bench.campaign import run_campaign

    monkeypatch.setenv("RESULTS_PATH", str(tmp_path))
    desc = tmp_path / "c.json"
    desc.write_text(json.dumps({
        "name": "legacy",
        "phases": [{"name": "p", "runs": [
            {"kind": "quality", "model": "rng",
             "data": "share/benchmarks/quality/questions.json",
             "questions": 2, "syntaxes": ["complete"], "demos": ["select"],
             "out": "partial"}]}]}))
    out = tmp_path / "legacy" / "partial"
    out.mkdir(parents=True)
    (out / "results-oldhost-rng.ndjson").write_text(
        json.dumps({"event.action": "quality.run"}) + "\n")   # 1 of 2 events

    logs = []
    _, failures = run_campaign(str(desc), workers=[worker], log=logs.append)
    assert failures == []
    assert not any("skipping" in m for m in logs)              # partial re-ran
    files = list(out.glob("results-*.ndjson"))
    assert sum(1 for f in files for _ in open(f)) >= 3         # legacy + 2 new

    logs2 = []
    run_campaign(str(desc), workers=[worker], log=logs2.append)
    assert any("skipping" in m for m in logs2)                 # now complete


def test_tag_derivation():
    from autocog.bench.workers import model_tag
    assert model_tag(None) == "rng"
    assert model_tag("Llama-3.2-1B") == "Llama-3.2-1B"          # dotted tag intact
    assert model_tag("models/Llama-3.2-1B.Q8_0.gguf") == "Llama-3.2-1B.Q8_0"
    assert model_tag("x.gguf") == "x"


class TestLauncher:
    """share/experiments/campaign.sh on a scratch workdir with the test
    campaign: dryrun plan, full run (multi-model worker, executor routing,
    probe phase, perf termination cells, archive), and resume."""

    @pytest.fixture()
    def workdir(self, tmp_path, repo_root, tiny_gguf):
        wd = tmp_path / "wd"
        for sub in ("models", "datasets", "campaigns"):
            (wd / sub).mkdir(parents=True)
        (wd / "autocog").symlink_to(repo_root)
        venv = repo_root / "venv"
        if not venv.exists():
            pytest.skip("no venv beside the repo")
        (wd / ".venv").symlink_to(venv)
        shutil.copy(tiny_gguf, wd / "models" / (TINY + ".gguf"))
        shutil.copy(repo_root / "share" / "benchmarks" / "quality" / "questions.json",
                    wd / "datasets" / "questions.json")
        shutil.copy(repo_root / "tests" / "fixtures" / "campaigns" / "testing.json",
                    wd / "campaigns" / "testing.json")
        return wd

    def launch(self, wd, *args, timeout):
        script = wd / "autocog" / "share" / "experiments" / "campaign.sh"
        env = {k: v for k, v in os.environ.items()
               if k not in ("AUTOCOG_WORKDIR", "MODELS_PATH", "DATASETS_PATH",
                            "RESULTS_PATH", "AUTOCOG_REPO")}
        return subprocess.run([str(script), *args, "campaigns/testing.json"],
                              cwd=str(wd), env=env, capture_output=True,
                              text=True, timeout=timeout)

    def test_dryrun(self, workdir):
        r = self.launch(workdir, "--dryrun", timeout=120)
        assert r.returncode == 0, r.stdout + r.stderr
        assert f"models=[{TINY}]" in r.stdout
        assert "phase smoke [bench]" in r.stdout
        assert "phase probes [probe]" in r.stdout
        assert not (workdir / "results").exists()   # nothing spawned

    @pytest.mark.timeout(600)
    def test_full_run_and_resume(self, workdir):
        r = self.launch(workdir, "--ngl", "0", timeout=580)
        assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-2000:]
        out = workdir / "results" / "testing"
        for run in ("rng-select", "tiny-select", "rng-term"):
            assert list((out / run).glob("results-*.ndjson")), run
        assert (out / "tiny-probe.ndjson").is_file()
        assert json.loads((out / "adjudication.json").read_text())
        # termination cells: the stop predicate cut eval tokens on len300
        events = [json.loads(l) for l in
                  next((out / "rng-term").glob("results-*.ndjson")).open()]
        by = {e["autocog.bench.label"]: e for e in events}
        assert (by["len300 w2 stop-term-16"]["autocog.perf.tokens.eval"]
                < by["len300 w2 baseline"]["autocog.perf.tokens.eval"])
        assert list((workdir / "results").glob("testing-*.tar.gz"))

        # Event stream + status screen: every run has lifecycle events and
        # the monitor renders progress, rates and axis-sliced accuracy.
        events = workdir / "results" / "campaign-events.ndjson"
        assert events.is_file()
        stream = [json.loads(l) for l in events.read_text().splitlines()]
        actions = [(e.get("autocog.bench") or e).get("event.action") for e in stream]
        assert actions.count("run.end") == 4          # incl. the probe run
        assert "campaign.plan" in actions and "campaign.end" in actions
        assert any(a == "quality.run" for a in actions)
        mon = subprocess.run(
            [sys.executable, str(workdir / "autocog" / "share" / "benchmarks"
                                 / "monitor.py"), "--once", str(events)],
            capture_output=True, text=True, timeout=60)
        assert mon.returncode == 0, mon.stderr
        assert "4/4 runs" in mon.stdout
        assert "accuracy by model" in mon.stdout
        assert "accuracy by demo" in mon.stdout

        r2 = self.launch(workdir, "--ngl", "0", timeout=300)
        assert r2.returncode == 0
        assert r2.stdout.count("skipping") >= 4   # every run resumed as done


def test_build_options_binding():
    """The backend reports its build flags structurally (calibration gates
    GPU boxes on the cuda/rocm/vulkan flags)."""
    from autocog.backend.llama import backend_llama_cxx as b

    opts = dict(b.build_options())
    assert set(opts) == {"build_type", "cuda", "rocm", "blas", "vulkan",
                         "metal", "native", "tuned"}
    assert isinstance(opts["cuda"], bool)
    assert opts["build_type"] in ("Debug", "Release", "RelWithDebInfo",
                                  "MinSizeRel")
    # Coherence with the human-readable form.
    info = b.build_info()
    assert (("cuda:        yes" in info) == opts["cuda"])
