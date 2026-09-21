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

    # The EFFECTIVE slot count, not the requested one. `TINY` was loaded with
    # no kv_slots at all and still runs the built-in default; advertising None
    # there would mean a campaign recording this worker's configuration
    # recorded nothing, for a value that changes free-text sampling and moves
    # every logprob (see test_kv_slots.py). The worker must be able to say
    # what it is actually running.
    assert caps["details"][TINY]["kv_slots"] >= 1
    assert caps["details"]["rng"]["kv_slots"] is None      # no KV cache
    assert caps["kv_slots"] == caps["details"][TINY]["kv_slots"]

    from autocog.bench.workers import RemoteWorker
    import autocog
    from autocog.runtime.sta import runtime_sta_cxx as rt

    prog = autocog.compile("share/demos/mcq/strings-select.stl",
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
    for b in pool.backends.values():
        orig = b.evaluate_prompt_async

        async def wrapped(*a, _url=b.server_url, _orig=orig, **kw):
            counts[_url] = counts.get(_url, 0) + 1
            return await _orig(*a, **kw)

        b.evaluate_prompt_async = wrapped

    prog = autocog.compile("share/demos/mcq/strings-select.stl",
                           includes=["share/demos/mcq"])

    async def one(i):
        return await pool.run_async(prog, topic="t", question=f"q{i}",
                                    choice=[{"label": "A", "value": "a"}, {"label": "B", "value": "b"}, {"label": "C", "value": "c"}])

    async def many():
        return await asyncio.gather(*(one(i) for i in range(6)))

    results = asyncio.run(many())
    assert len(results) == 6
    assert all(r in ("a", "b", "c") for r in results)   # select returns the answer
    assert len(counts) == 2 and sum(counts.values()) == 6   # both lanes used

    # Status API: both workers accounted their jobs.
    for url in rng_workers:
        with urllib.request.urlopen(f"{url}/stats") as r:
            stats = json.loads(r.read())
        assert stats["lanes"] == 1
        assert stats["total"] > 0 and stats["errors"] == 0
        one_m = stats["windows"]["1m"]
        assert one_m["jobs"] > 0 and one_m["rate"] > 0
        assert 0.0 <= one_m["occupancy"] <= 1.0
        assert set(stats["windows"]) == {"1m", "5m", "30m"}


def test_pooled_scoring_measures_friction(rng_workers, tmp_path):
    """Friction over the wire: a pooled run must come back with the same
    per-token numbers the in-process engine produces.

    This is the path every campaign takes, and it is the one where a silent
    zero costs the most — a whole run of samples that look scored and are
    not. The worker has no scoring endpoint of its own: the client
    re-renders the frame into a forced FTA and asks /evaluate to score all
    of it, so what is exercised here is the same code the local engine runs.
    """
    from autocog.bench.quality import run_quality

    events = run_quality(
        questions=2, syntaxes=["complete"],
        programs=["share/demos/mcq/strings-select.stl"],
        out=str(tmp_path / "pooled"), workers=rng_workers,
        log=lambda *_: None)

    assert len(events) == 2
    for ev in events:
        assert ev.get("error.message") is None, ev.get("error.message")
        assert not ev.get("autocog.bench.score_error"), \
            ev.get("autocog.bench.score_error")
        assert ev["autocog.bench.steps"] > 0
        assert ev["autocog.bench.tokens.value"] > 0
        assert ev["autocog.bench.tokens.structure"] > 0
        assert ev["autocog.bench.friction.value"]
        assert ev["autocog.bench.friction.structure"]


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
             "questions": 2, "syntaxes": ["complete"],
             "programs": ["share/demos/mcq/strings-select.stl"],
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

    # No duplicate samples across all files after the resume pass.
    keys = []
    for f in out.glob("results-*.ndjson"):
        for line in f.open():
            ev = json.loads(line)
            if ev.get("autocog.bench.question") is not None:
                keys.append((ev["autocog.bench.syntax"], ev["autocog.bench.program"],
                             ev["autocog.bench.question"]))
    assert len(keys) == 2 and len(set(keys)) == 2


def test_scheduler_no_starvation():
    """The scan skips saturated tag groups: a big slow run on tag A must
    not stop tag B's lanes from being fed (B finishes long before A)."""
    import asyncio

    from autocog.bench.quality import run_samples

    class FakeLedger:
        def total_lanes(self):
            return 1

    class FakeRun:
        def __init__(self, tag, n, delay):
            self.tag, self.delay = tag, delay
            self.pending = [("s", "d", {"id": i}) for i in range(n)]
            self.emitted = []

        async def eval_sample(self, pools, syntax, demo, q):
            await asyncio.sleep(self.delay)
            return {"q": q["id"]}

        def emit(self, ev):
            self.emitted.append(ev)

        def finish(self):
            pass

    slow = FakeRun("A", 12, 0.05)
    fast = FakeRun("B", 3, 0.001)
    ends = []
    asyncio.run(run_samples(
        [(slow, None), (fast, None)],
        {"A": FakeLedger(), "B": FakeLedger()},
        log=lambda m: None, window=1,
        on_end=lambda i: ends.append(i)))
    assert ends == [1, 0]                     # B (index 1) done first
    assert len(slow.emitted) == 12 and len(fast.emitted) == 3


def test_resume_retries_failed_samples(tmp_path, worker, monkeypatch):
    """A sample recorded with an error is NOT a measurement: a rerun must
    retry it. Otherwise a transient worker failure is permanent — the
    rerun skips the hole and the run ships incomplete."""
    from autocog.bench.campaign import run_campaign

    monkeypatch.setenv("RESULTS_PATH", str(tmp_path))
    desc = tmp_path / "c.json"
    desc.write_text(json.dumps({
        "name": "retry",
        "phases": [{"name": "p", "runs": [
            {"kind": "quality", "model": "rng",
             "data": "share/benchmarks/quality/questions.json",
             "questions": 2, "syntaxes": ["complete"],
             "programs": ["share/demos/mcq/strings-select.stl"],
             "out": "r"}]}]}))
    out = tmp_path / "retry" / "r"
    out.mkdir(parents=True)

    # one sample previously failed, one previously succeeded
    import json as _json
    from autocog.bench.formatters import load_formatter, load_questions
    qs = load_questions("share/benchmarks/quality/questions.json",
                        load_formatter(""), limit=2)
    (out / "results-old.ndjson").write_text("".join(_json.dumps(r) + "\n" for r in [
        {"autocog.bench.syntax": "complete", "autocog.bench.program": "strings-select",
         "autocog.bench.question": qs[0]["id"], "autocog.bench.correct": True},
        {"autocog.bench.syntax": "complete", "autocog.bench.program": "strings-select",
         "autocog.bench.question": qs[1]["id"], "autocog.bench.correct": None,
         "error.message": "RemoteError: worker died"},
    ]))

    from autocog.bench.quality import existing_samples
    have = existing_samples(str(out))
    assert ("complete", "strings-select", qs[0]["id"]) in have      # measured
    assert ("complete", "strings-select", qs[1]["id"]) not in have  # failed -> retry

    _, failures = run_campaign(str(desc), workers=[worker], log=lambda m: None)
    assert failures == []
    fresh = [json.loads(l) for f in out.glob("results-*.ndjson")
             if f.name != "results-old.ndjson" for l in f.open()]
    redone = [e for e in fresh if e.get("autocog.bench.question") == qs[1]["id"]]
    assert len(redone) == 1 and redone[0]["autocog.bench.correct"] is not None
    assert not [e for e in fresh
                if e.get("autocog.bench.question") == qs[0]["id"]]   # not redone


def test_tag_derivation():
    from autocog.bench.workers import model_tag
    assert model_tag(None) == "rng"
    assert model_tag("Llama-3.2-1B") == "Llama-3.2-1B"          # dotted tag intact
    assert model_tag("models/Llama-3.2-1B.Q8_0.gguf") == "Llama-3.2-1B.Q8_0"
    assert model_tag("x.gguf") == "x"


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
