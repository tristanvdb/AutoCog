"""Engine perf/score surface — the `autocog bench` phase-1 bindings.

The validation gate: the perf dict the Engine returns must be the SAME
field map xfta --perf emits, with equal integer counters on an identical
evaluation (same FTA, model, seed). Float durations only need to exist.
"""

import json
import math
import os
import subprocess
import tempfile

import pytest

import autocog

def repo(p, *parts):
    return str(p.joinpath(*parts))


def build_dir(repo_root):
    for cand in ("build-rel", "build"):
        xfta = repo_root / cand / "tools" / "xfta" / "xfta"
        if xfta.exists():
            return repo_root / cand
    return None


@pytest.fixture
def engine(repo_root):
    return autocog.Engine(
        syntax=repo(repo_root, "share", "syntax", "complete.json"),
        search=repo(repo_root, "share", "search", "default.json"),
    )


INT_KEYS = [
    "autocog.perf.text.calls", "autocog.perf.complete.calls",
    "autocog.perf.choose.calls",
    "autocog.perf.text.tokens.eval", "autocog.perf.complete.tokens.eval",
    "autocog.perf.choose.tokens.eval",
    "autocog.perf.tokens.restore", "autocog.perf.tokens.eval",
    "autocog.perf.kv.slots", "autocog.perf.kv.forks",
    "autocog.perf.decode.calls",
    "autocog.perf.search.terminals",
]


class TestPerfSurface:
    def test_last_perf_populated(self, engine, repo_root):
        prog = autocog.compile(repo(repo_root, "share", "demos", "mcq", "strings-select.stl"))
        assert engine.last_perf is None
        engine.set_seed(42)
        engine.run(prog, topic="Sci", question="2+2?", choice=[{"label": "A", "value": "3"}, {"label": "B", "value": "4"}, {"label": "C", "value": "5"}, {"label": "D", "value": "6"}])
        perf = engine.last_perf
        assert perf is not None
        for key in INT_KEYS:
            assert key in perf, key
        assert perf["autocog.perf.tokens.eval"] > 0
        assert perf["autocog.perf.advance_seconds"] > 0

    def test_reset_and_deltas(self, engine, repo_root):
        """Per-eval deltas: two identical evaluations report identical token
        counters (the second must not include the first's accumulation)."""
        prog = autocog.compile(repo(repo_root, "share", "demos", "mcq", "strings-select.stl"))
        inputs = dict(topic="Sci", question="2+2?", choice=[{"label": "A", "value": "3"}, {"label": "B", "value": "4"}, {"label": "C", "value": "5"}, {"label": "D", "value": "6"}])
        engine.set_seed(42)
        engine.run(prog, **inputs)
        first = engine.last_perf
        engine.reset()
        engine.set_seed(42)
        engine.run(prog, **inputs)
        second = engine.last_perf
        for key in INT_KEYS:
            assert first[key] == second[key], key

    def test_matches_xfta(self, engine, repo_root):
        """The gate: identical FTA + seed through the Engine and through the
        xfta binary must yield identical integer counters and identical keys."""
        bdir = build_dir(repo_root)
        if bdir is None:
            pytest.skip("no built xfta binary")

        from autocog.runtime.sta import runtime_sta_cxx
        from autocog.backend.llama import backend_llama_cxx

        prog = autocog.compile(repo(repo_root, "share", "demos", "mcq", "strings-select.stl"))
        content = {"topic": "Sci", "question": "2+2?",
                   "choice": [{"label": "A", "value": "3"}, {"label": "B", "value": "4"}, {"label": "C", "value": "5"}, {"label": "D", "value": "6"}]}
        fta_id = runtime_sta_cxx.instantiate(
            prog.id, "main", content, engine.syntax_id, engine.search_id)
        try:
            with tempfile.TemporaryDirectory() as tmp:
                fta_file = os.path.join(tmp, "gate.fta")
                with open(fta_file, "w") as f:
                    f.write(runtime_sta_cxx.dump_fta(fta_id))

                engine.set_seed(42)
                ftt_id, perf_json = backend_llama_cxx.evaluate(engine.model_id, fta_id)
                runtime_sta_cxx.release_ftt(ftt_id)
                mine = json.loads(perf_json)

                perf_file = os.path.join(tmp, "perf.ndjson")
                subprocess.run(
                    [str(bdir / "tools" / "xfta" / "xfta"), "--rng", "--seed", "42",
                     "--fta", fta_file, "--ftt", os.path.join(tmp, "out.ftt"),
                     "--perf", perf_file],
                    check=True, capture_output=True)
                events = [json.loads(l) for l in open(perf_file)]
                theirs = next(e for e in events if e["event.action"] == "eval.summary")

                for key in INT_KEYS:
                    assert mine[key] == theirs[key], key
                # same field map: every perf key xfta emits exists here too
                for key in theirs:
                    if key.startswith("autocog.perf.") and "host" not in key \
                       and "build_type" not in key and "model" not in key \
                       and "seed" not in key and "fta.path" not in key:
                        assert key in mine, key
        finally:
            runtime_sta_cxx.release_fta(fta_id)


class TestPerfSurfaceRealModel:
    def test_matches_xfta_tiny_model(self, repo_root):
        """Same gate on a real (tiny) model: decode/sample counters engaged."""
        bdir = build_dir(repo_root)
        model = repo_root / "models" / "tiny-llama3-test-Q2_K.gguf"
        if bdir is None or not model.exists():
            pytest.skip("needs built xfta + tiny model")

        from autocog.runtime.sta import runtime_sta_cxx
        from autocog.backend.llama import backend_llama_cxx

        engine = autocog.Engine(
            model=str(model),
            syntax=repo(repo_root, "share", "syntax", "complete.json"),
            search=repo(repo_root, "share", "search", "default.json"),
        )
        prog = autocog.compile(repo(repo_root, "share", "demos", "mcq", "strings-select.stl"))
        content = {"topic": "Sci", "question": "2+2?",
                   "choice": [{"label": "A", "value": "3"}, {"label": "B", "value": "4"}, {"label": "C", "value": "5"}, {"label": "D", "value": "6"}]}
        fta_id = runtime_sta_cxx.instantiate(
            prog.id, "main", content, engine.syntax_id, engine.search_id)
        try:
            with tempfile.TemporaryDirectory() as tmp:
                fta_file = os.path.join(tmp, "gate.fta")
                with open(fta_file, "w") as f:
                    f.write(runtime_sta_cxx.dump_fta(fta_id))

                engine.set_seed(42)
                ftt_id, perf_json = backend_llama_cxx.evaluate(engine.model_id, fta_id)
                runtime_sta_cxx.release_ftt(ftt_id)
                mine = json.loads(perf_json)
                assert mine["autocog.perf.decode.calls"] > 0

                perf_file = os.path.join(tmp, "perf.ndjson")
                subprocess.run(
                    [str(bdir / "tools" / "xfta" / "xfta"), "--model", str(model),
                     "--seed", "42", "--fta", fta_file,
                     "--ftt", os.path.join(tmp, "out.ftt"), "--perf", perf_file],
                    check=True, capture_output=True)
                events = [json.loads(l) for l in open(perf_file)]
                theirs = next(e for e in events if e["event.action"] == "eval.summary")
                for key in INT_KEYS:
                    assert mine[key] == theirs[key], key
        finally:
            runtime_sta_cxx.release_fta(fta_id)


def spine(node):
    """Flatten a single-path FTT to its nodes, asserting it stays single."""
    out = []
    while True:
        out.append(node)
        kids = node.get("children") or []
        assert len(kids) <= 1, f"not a forced path: {node.get('uid')}"
        if not kids:
            return out
        node = kids[0]


def best_path(node, prefix=()):
    """The path the frame came from: the completed leaf with the best score.

    Mirrors walk_ftt_to_frame's "best" metric — exp(-logprob/length) over
    leaves that were not pruned. A generation may branch; a frame names one
    branch, so this is what a re-rendering has to reproduce.
    """
    path = prefix + (node,)
    kids = node.get("children") or []
    if not kids:
        if node.get("pruned") is not None:
            return None, -1.0
        length = node.get("length") or 0
        score = math.exp(-node["logprob"] / length) if length else 0.0
        return path, score
    best, best_score = None, -1.0
    for kid in kids:
        candidate, score = best_path(kid, path)
        if candidate is not None and score > best_score:
            best, best_score = candidate, score
    return best, best_score


class TestScoreFrame:
    def test_score_frame_reproduces_the_generation(self, engine, repo_root):
        """Re-rendering a recorded frame must reproduce the tokens the model
        actually read — that is the whole claim behind faithful frames.

        The recorded FTT is the generation; score_frame instantiates the
        prompt with the frame as content and scores the result. If the two
        token streams differ by so much as one token, every friction number
        measured this way is measuring a prompt nobody ever ran.
        """
        from autocog.recorder import Recorder

        prog = autocog.compile(repo(repo_root, "share", "demos", "mcq", "strings-select.stl"))
        inputs = dict(topic="Sci", question="2+2?",
                      choice=[{"label": "A", "value": "3"},
                              {"label": "B", "value": "4"},
                              {"label": "C", "value": "5"},
                              {"label": "D", "value": "6"}])
        with tempfile.TemporaryDirectory() as tmp:
            rec = Recorder(kinds={"input", "frame", "ftt"}, path=tmp)
            engine.set_seed(42)
            engine.run(prog, recorder=rec, **inputs)
            frames, contents, ftts = [], [], []
            for root, _, files in os.walk(tmp):
                frames += [os.path.join(root, f) for f in files
                           if f.endswith(".frame.json")]
                contents += [os.path.join(root, f) for f in files
                             if f.endswith(".input.json")]
                ftts += [os.path.join(root, f) for f in files
                         if f.endswith(".ftt.json")]
            assert frames and contents and ftts
            frame = json.load(open(frames[0]))
            # CONTENT, not inputs: re-rendering needs the resolved channel
            # values (the choice texts this program pulled out of the
            # records), which is what the recorder captured. This is the
            # same pairing bench/quality.py feeds score_frame.
            content = json.load(open(contents[0]))
            generated = json.load(open(ftts[0]))

        scored = engine.score_frame(prog, "main", frame, content)

        # The generation may have branched (search); the frame names one path,
        # so compare against the branch the walk chose.
        want, _ = best_path(generated)
        assert want is not None
        got = spine(scored)
        assert "".join(n["text"] for n in got) == "".join(n["text"] for n in want)
        assert [t for n in got for t in n["tokens"]] == \
               [t for n in want for t in n["tokens"]]

        # Scored: every node below the root carries one logprob per token, and
        # they are not uniformly zero (which is what an unarmed score_all
        # would leave behind).
        below = got[1:]
        assert below
        for n in below:
            assert len(n["logprobs"]) == len(n["tokens"]), n.get("uid")
        assert any(v != 0.0 for n in below for v in n["logprobs"])

    def test_perf_record_kind(self, engine, repo_root):
        from autocog.recorder import Recorder

        prog = autocog.compile(repo(repo_root, "share", "demos", "mcq", "strings-select.stl"))
        with tempfile.TemporaryDirectory() as tmp:
            rec = Recorder(kinds={"perf"}, path=tmp)
            engine.set_seed(42)
            engine.run(prog, recorder=rec,
                       topic="Sci", question="2+2?", choice=[{"label": "A", "value": "3"}, {"label": "B", "value": "4"}, {"label": "C", "value": "5"}, {"label": "D", "value": "6"}])
            perfs = []
            for root, _, files in os.walk(tmp):
                perfs += [os.path.join(root, f) for f in files
                          if f.endswith(".perf.json")]
            assert perfs
            perf = json.load(open(perfs[0]))
            assert perf["autocog.perf.tokens.eval"] > 0
