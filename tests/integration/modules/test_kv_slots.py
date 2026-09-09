"""KV sequence-slot pool equivalence.

The backend multiplexes N llama sequences over one context so sibling branches
stay resident during beam/choice ping-pong (forked via llama_memory_seq_cp)
instead of being re-decoded on every switch. That is a pure caching layer: the
distributions sampled must be the ones a single re-decoded sequence would have
produced. AUTOCOG_KV_SLOTS=1 degenerates to the historical single-sequence
trim-and-redecode behavior, so running the same beam-searchy program under
both settings must yield the same output.

The variable is read at model load, so each setting runs in its own
subprocess against the real tiny model.
"""

import json
import os
import subprocess
import sys

import pytest

RUNNER = """
import json, sys
import autocog

stl, model, syntax, search = sys.argv[1:5]
prog = autocog.compile(stl)
engine = autocog.Engine(model=model, syntax=syntax, search=search, n_ctx=2048)
engine.set_seed(42)
result = engine.run(prog, topic="Science", question="What is H2O?",
                    choices=["Water", "Fire", "Air", "Earth"])
print(json.dumps(result, sort_keys=True, default=str))
"""

PROGRAM = """
prompt main {
  is {
    topic is text<length=10>;
    question is text<length=20>;
    choices[4] is text<length=10>;
    work[1:3] is text<length=8>;
    answer is select(choices);
  }
  channel {
    topic get topic;
    question get question;
    choices get choices;
  }
  return {
    use work;
    use answer;
  }
}

export main;
"""


def _run_with_slots(slots, stl, model, syntax, search):
    env = dict(os.environ, AUTOCOG_KV_SLOTS=str(slots))
    r = subprocess.run(
        [sys.executable, "-c", RUNNER, stl, model, syntax, search],
        capture_output=True, text=True, timeout=600, env=env)
    assert r.returncode == 0, f"slots={slots} failed:\n{r.stderr[-2000:]}"
    return json.loads(r.stdout.strip().splitlines()[-1])


@pytest.mark.timeout(900)  # two real-model beam searches; slow on a loaded machine
def test_slot_pool_matches_single_sequence(tmp_path, llama3_model_path,
                                           syntax_path):
    stl = tmp_path / "kv.stl"
    stl.write_text(PROGRAM)
    # Beam search wide enough to force branch ping-pong, lookahead rollouts
    # (fork + trim), and surviving-width forks; small budgets keep it fast.
    search = tmp_path / "search.json"
    search.write_text(json.dumps({
        "text": {"threshold": 0.1, "beams": 4, "ahead": 2, "width": 2,
                 "repetition": None, "diversity": None},
        "enum": {"threshold": 0.1, "width": 1},
        "branch": {"threshold": 0.1, "width": 1},
        "flow": {"threshold": 0.1, "width": 1},
        "queue": {"metric": "perplexity"},
    }))

    pooled = _run_with_slots(16, str(stl), llama3_model_path, syntax_path, str(search))
    legacy = _run_with_slots(1, str(stl), llama3_model_path, syntax_path, str(search))
    assert pooled == legacy, (
        f"slot pool changed the sampled output:\n  16 slots: {pooled!r}\n"
        f"   1 slot:  {legacy!r}")
