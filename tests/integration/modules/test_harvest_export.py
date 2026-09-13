"""Harvest + export: recorded run -> re-rendered traces -> training JSONL.

The chain under test is the v0.7 arc's data pipeline: run a program once
(recorder captures input/frame per step), harvest re-renders every step
under several syntaxes (ista + efta), and export flattens each rendering
into a token sequence with a loss mask. The load-bearing assertions:

  - a harvested FTT parses back (psta walk) to the recorded frame — the
    round-trip property, now through the *recorder* path with resolved
    select values, under a syntax the run never used;
  - mask policies partition the same token sequence (value + structure =
    all, prompt always context).
"""

import json
import os
import subprocess
import sys

import pytest

PROGRAM = '''
prompt main {
  is {
    topic is text<length=10>;
    question is text<length=20>;
    choices[4] is text<length=10>;
    thought is text<length=15>;
    answer is select(choices);
  }
  channel {
    topic get topic;
    question get question;
    choices get choices;
  }
  return {
    use answer;
  }
}

export main;
'''

INPUTS = {"topic": "Science", "question": "What is H2O?",
          "choices": ["Water", "Fire", "Air", "Earth"]}


@pytest.fixture(scope="module")
def build_dir(request):
    root = request.config.rootpath
    for candidate in ("build", "build-release"):
        d = os.path.join(str(root), candidate)
        if os.path.exists(os.path.join(d, "tools", "efta", "efta")):
            return d
    pytest.skip("no build tree with efta available")


def test_harvest_and_export(tmp_path, engine, repo_root, build_dir):
    import autocog
    from autocog.recorder import Recorder
    from autocog.harvest import harvest
    from autocog.export import export

    stl = tmp_path / "prog.stl"
    stl.write_text(PROGRAM)
    prog = autocog.compile(str(stl))
    rec = Recorder(kinds={"input", "frame"}, path=str(tmp_path / "records"))
    engine.set_seed(42)
    engine.run(prog, recorder=rec, **INPUTS)

    out = tmp_path / "harvest"
    manifest = harvest(str(rec.path), str(stl), str(out),
                       syntaxes=["complete", "special"], rng=True,
                       tools_dir=build_dir, share=str(repo_root / "share"))
    assert manifest["version"] == 1
    assert len(manifest["steps"]) == 2  # 1 recorded step x 2 syntaxes

    # Round trip through a syntax the run never used: the harvested FTT must
    # walk back to the recorded frame's values (select resolved to "Water"
    # etc. by the engine; the encoder inverted it via the input content).
    for step in manifest["steps"]:
        ftt = out / step["ftt"]
        frame_out = tmp_path / f"frame-{step['syntax']}.json"
        r = subprocess.run([os.path.join(build_dir, "tools", "psta", "psta"),
                            "--sta", str(out / "program.sta"), "--ftt", str(ftt),
                            "--prompt", step["prompt"], "--frame", str(frame_out)],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr[-500:]
        got = json.loads(frame_out.read_text())
        want = json.loads((out / step["frame"]).read_text())
        # psta keeps raw select indices; the recorded frame resolved them.
        # Every non-select value must match exactly; the select field must
        # point at the recorded value's index in the choices.
        assert got["topic"] == want["topic"]
        assert got["thought"] == want["thought"]
        assert INPUTS["choices"][int(got["answer"])] == want["answer"]

    # Export under each mask policy: same tokens, complementary masks.
    rows = {}
    for policy in ("all", "value", "structure"):
        path = tmp_path / f"data-{policy}.jsonl"
        n = export(str(out), str(path), policy=policy)
        assert n == 2
        lines = [json.loads(l) for l in path.read_text().splitlines()]
        assert lines[0]["format"] == "autocog-dataset"
        rows[policy] = lines[1:]

    for a, v, s in zip(rows["all"], rows["value"], rows["structure"]):
        assert a["tokens"] == v["tokens"] == s["tokens"]
        assert len(a["tokens"]) == len(a["mask"])
        n_prompt = len(a["mask"]) - sum(a["mask"])  # all-policy zeros = prompt
        for i in range(len(a["mask"])):
            assert v["mask"][i] + s["mask"][i] == a["mask"][i]
        assert sum(v["mask"]) > 0 and sum(s["mask"]) > 0
        assert n_prompt > 0  # the prompt is context under every policy


def test_pipeline_cli_and_rejections(tmp_path, engine, repo_root, build_dir, capsys):
    """The harvest/export CLI entries (main) and their rejection arms:
    unknown mask policy, non-harvest directory, and the --syntaxes filter."""
    import autocog
    from autocog.recorder import Recorder
    from autocog import export as export_mod
    from autocog import harvest as harvest_mod

    stl = tmp_path / "prog.stl"
    stl.write_text(PROGRAM)
    prog = autocog.compile(str(stl))
    rec = Recorder(kinds={"input", "frame"}, path=str(tmp_path / "records"))
    engine.set_seed(42)
    engine.run(prog, recorder=rec, **INPUTS)

    out = tmp_path / "harvest"
    rc = harvest_mod.main([
        "--records", str(rec.path), "--stl", str(stl), "--out", str(out),
        "--syntaxes", "complete,special", "--rng",
        "--tools", build_dir, "--share", str(repo_root / "share")])
    assert rc == 0
    assert "2 step renderings" in capsys.readouterr().out

    data = tmp_path / "data.jsonl"
    rc = export_mod.main(["--harvest", str(out), "--out", str(data),
                          "--mask", "value", "--syntaxes", "special"])
    assert rc == 0
    assert "exported 1 examples" in capsys.readouterr().out
    lines = [json.loads(l) for l in data.read_text().splitlines()]
    assert len(lines) == 2  # header + the one 'special' row
    assert lines[1]["meta"]["syntax"] == "special"

    with pytest.raises(ValueError, match="unknown mask policy"):
        export_mod.export(str(out), str(data), policy="everything")
    not_harvest = tmp_path / "records"  # exists, but no harvest manifest
    with pytest.raises((ValueError, FileNotFoundError)):
        export_mod.export(str(not_harvest), str(data))
