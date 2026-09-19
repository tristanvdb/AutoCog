"""The launcher and the monitor: share/experiments/campaign end to end.

Part of the HARNESS suite, not the library's — a descriptor format or a
worker launcher failing is not a reason to call autocog broken.

    python3 -m pytest share/experiments/tests
"""

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
                 os.path.join(str(request.config.rootpath), "models"),
                 os.path.join(str(request.config.rootpath), "..", "data",
                              "models")):
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


def test_monitor_scopes_to_current_campaign(tmp_path):
    """One events file spans a whole session. Accuracy slices and rates
    must describe the CURRENT campaign only, and must parse both stamp
    formats in the stream (ECS '...Z' vs bench isoformat '+00:00')."""
    import datetime

    def iso(i):
        return (datetime.datetime(2026, 9, 16, 4, 0, 0,
                                  tzinfo=datetime.timezone.utc)
                + datetime.timedelta(seconds=i)).isoformat()

    lines = [
        {"event.action": "campaign.plan", "autocog.campaign.name": "old",
         "autocog.campaign.runs": 1, "axes": ["model"]},
        {"event.action": "campaign.start", "autocog.campaign.name": "old",
         "@timestamp": "2026-09-16T03:00:00.000Z"},
    ]
    lines += [{"@timestamp": "2026-09-16T03:00:00.000Z",
               "autocog.bench": {"event.action": "quality.run",
                                 "autocog.bench.model": "old-model",
                                 "autocog.bench.correct": True,
                                 "@timestamp": iso(i)}} for i in range(5)]
    lines += [
        {"event.action": "campaign.end", "autocog.campaign.name": "old",
         "@timestamp": "2026-09-16T03:10:00.000Z"},
        {"event.action": "campaign.plan", "autocog.campaign.name": "new",
         "autocog.campaign.runs": 2, "axes": ["model"]},
        {"event.action": "campaign.start", "autocog.campaign.name": "new",
         "@timestamp": "2026-09-16T04:00:00.000Z"},
    ]
    # bench events carry ONLY the isoformat stamp the writer produces
    lines += [{"@timestamp": iso(i),
               "autocog.bench": {"event.action": "quality.run",
                                 "autocog.bench.model": "new-model",
                                 "autocog.bench.correct": False,
                                 "@timestamp": iso(i)}} for i in range(10)]
    events = tmp_path / "events.ndjson"
    events.write_text("".join(json.dumps(l) + "\n" for l in lines))

    mon = subprocess.run(
        [sys.executable,
         os.path.join(os.path.dirname(__file__), "..", "campaign",
                      "monitor.py"),
         "--once", str(events)],
        capture_output=True, text=True, timeout=60)
    assert mon.returncode == 0, mon.stderr
    assert "new-model" in mon.stdout                 # current campaign sliced
    assert "old-model" not in mon.stdout             # finished one excluded
    assert "q/min" in mon.stdout                     # isoformat stamps parsed


def test_monitor_worker_stats(rng_workers, tmp_path):
    """The monitor polls advertised workers' /stats while the campaign
    runs, and distinguishes a dead worker from a busy one."""
    dead = f"127.0.0.1:{_free_port()}"
    events = tmp_path / "events.ndjson"
    lines = [
        # workers ride on the plan: the app knows its topology before it
        # spawns anything, so there is no separate announcement
        {"event.action": "campaign.plan", "autocog.campaign.name": "c",
         "autocog.campaign.runs": 1, "axes": [],
         "workers": [rng_workers[0].replace("http://", ""), dead]},
        {"event.action": "campaign.start", "autocog.campaign.name": "c",
         "@timestamp": "2026-09-15T00:00:00.000Z"},
    ]
    events.write_text("".join(json.dumps(l) + "\n" for l in lines))
    mon = subprocess.run(
        [sys.executable,
         os.path.join(os.path.dirname(__file__), "..", "campaign",
                      "monitor.py"),
         "--once", str(events)],
        capture_output=True, text=True, timeout=60)
    assert mon.returncode == 0, mon.stderr
    assert "workers:" in mon.stdout
    assert "fta/s" in mon.stdout
    # a worker that never answers is DOWN; a worker whose event loop is
    # merely busy must not be reported the same way (see fetch_stats)
    assert "down (no connection)" in mon.stdout
    assert "busy" not in mon.stdout


class TestLauncher:
    """share/experiments/campaign.sh's app on a scratch workdir: the plan it derives, a
    full run against workers it spawns itself, and resume.

    Exercised through `python3 -m campaign`, not the shell wrapper — the
    wrapper's whole job is the bootstrap (venv, build, pip), which this
    environment already has and which reinstalling would only make slow."""

    @pytest.fixture()
    def workdir(self, tmp_path, repo_root, tiny_gguf):
        wd = tmp_path / "wd"
        (wd / "models").mkdir(parents=True)
        (wd / "datasets").mkdir(parents=True)
        shutil.copy(tiny_gguf, wd / "models" / (TINY + ".gguf"))
        shutil.copy(repo_root / "share" / "benchmarks" / "quality" / "questions.json",
                    wd / "datasets" / "questions.json")
        shutil.copy(repo_root / "tests" / "fixtures" / "campaigns" / "testing.yml",
                    wd / "testing.yml")
        return wd

    def launch(self, wd, repo_root, *args, timeout):
        env = {k: v for k, v in os.environ.items()
               if k not in ("AUTOCOG_WORKDIR", "MODELS_PATH", "DATASETS_PATH",
                            "RESULTS_PATH")}
        env["PYTHONPATH"] = str(repo_root / "share" / "experiments")
        return subprocess.run(
            [sys.executable, "-m", "campaign", "testing.yml",
             "--workdir", str(wd), *args],
            cwd=str(wd), env=env, capture_output=True, text=True,
            timeout=timeout)

    def test_dry_run(self, workdir, repo_root):
        r = self.launch(workdir, repo_root, "--dry-run", timeout=120)
        assert r.returncode == 0, r.stdout + r.stderr
        assert "campaign testing" in r.stdout
        assert TINY in r.stdout
        assert "rng-mechanism" in r.stdout and "tiny-shape" in r.stdout
        assert not (workdir / "results").exists()   # nothing spawned

    @pytest.mark.timeout(900)
    def test_full_run_and_resume(self, workdir, repo_root):
        r = self.launch(workdir, repo_root, timeout=880)
        assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-2000:]

        out = workdir / "results" / "testing"
        for run in ("rng-mechanism", "tiny-shape"):
            assert list((out / run).glob("results-*.ndjson")), run

        # The gate ran and passed before any run started, and it wrote
        # nothing into the campaign's results.
        assert "gate:" in r.stdout
        assert r.stdout.index("gate:") < r.stdout.index("rng-mechanism]")

        events = workdir / "results" / "campaign-events.ndjson"
        assert events.is_file()
        stream = [json.loads(l) for l in events.read_text().splitlines()]
        actions = [(e.get("autocog.bench") or e).get("event.action") for e in stream]
        assert actions.count("run.end") == 2
        assert "campaign.plan" in actions and "campaign.end" in actions

        mon = subprocess.run(
            [sys.executable, str(repo_root / "share" / "experiments" / "campaign" / "monitor.py"),
             "--once", str(events)],
            capture_output=True, text=True, timeout=60)
        assert mon.returncode == 0, mon.stderr
        assert "accuracy by program" in mon.stdout
        # labels are an axis: both mechanisms are tagged `strings`
        assert "accuracy by label" in mon.stdout and "strings" in mon.stdout

        # Resume does no work twice: a measured sample is written once.
        r2 = self.launch(workdir, repo_root, timeout=880)
        assert r2.returncode == 0, r2.stdout[-2000:]
        for path in out.rglob("results-*.ndjson"):
            rows = [json.loads(l) for l in path.open() if l.strip()]
            keys = [(e.get("autocog.bench.syntax"), e.get("autocog.bench.program"),
                     e.get("autocog.bench.question")) for e in rows
                    if not e.get("error.message")]
            keys = [k for k in keys if all(p is not None for p in k)]
            assert len(keys) == len(set(keys)), f"{path.name}: resume duplicated samples"
