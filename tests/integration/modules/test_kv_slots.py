"""KV sequence-slot pool: what the cache does and does not preserve.

The backend multiplexes N llama sequences over one context so sibling branches
stay resident during beam/choice ping-pong (forked via llama_memory_seq_cp)
instead of being re-decoded on every switch. AUTOCOG_KV_SLOTS=1 degenerates to
single-sequence trim-and-redecode.

This used to assert that the two produce identical output, and xfailed. The
assertion was false, and not because slot recycling is wrong:

  * every slot count is internally DETERMINISTIC (16 twice gives 16);
  * every slot count disagrees with every other on free text -- 1 vs 2, 2 vs 8
    and 8 vs 16 all differ, so 1 is not a reference that 16 fails to match, it
    is one point in a family;
  * the tree SHAPE is identical across all of them: same nodes, same uids,
    same order. The cache changes nothing about what gets evaluated;
  * where the tokens agree the logprobs still drift, by up to ~3e-3.

That is the signature of a decode path that is not bit-reproducible across
different batchings, which is what caching necessarily changes: the same token
is decoded as a one-token delta on a forked sequence in one configuration and
as part of a long fresh decode in another, and a quantized matmul does not
promise those agree to the last bit. The drift then occasionally flips a
near-tie in a free-text beam.

So the invariants asserted here are the ones the cache actually owes, and
free-text token identity is explicitly not among them.

CONSEQUENCE FOR MEASUREMENT, which is the reason this is worth a test at all:
slot count is an experimental variable. Two workers configured differently can
answer the same question differently, and every logprob -- so every friction
number -- shifts a little. It has to be pinned and recorded per campaign.

The variable is read at model load, so each setting runs in its own subprocess
against the real tiny model.
"""

import json
import os
import subprocess
import sys

import pytest

# Emits a digest of the whole evaluation tree, not just the program result:
# shape, tokens and logprobs, so a difference can be attributed rather than
# just observed.
RUNNER = """
import json, sys
import autocog
from autocog.runtime.sta import runtime_sta_cxx as sta
from autocog.backend.llama import backend_llama_cxx as be

stl, model, syntax, search = sys.argv[1:5]
prog = autocog.compile(stl)
engine = autocog.Engine(model=model, syntax=syntax, search=search, n_ctx=2048)
engine.set_seed(42)
content = {"topic": "Science", "question": "What is H2O?",
           "choices": ["Water", "Fire", "Air", "Earth"]}
fta = sta.instantiate(prog.id, "main", content, engine.syntax_id,
                      engine.search_id, simplify="none")
ftt_id, _perf = be.evaluate(engine.model_id, fta)
ftt = sta.get_ftt(ftt_id)

shape, tokens, logprobs = [], [], []
def walk(n, path):
    shape.append([path, n["uid"]])
    tokens.append(n["tokens"])
    logprobs.append([round(x, 7) for x in n["logprobs"]])
    for i, c in enumerate(n.get("children") or []):
        walk(c, path + "." + str(i))
walk(ftt, "")

frame = sta.walk_ftt_to_frame(prog.id, "main", ftt_id, content)
print(json.dumps({"shape": shape, "tokens": tokens, "logprobs": logprobs,
                  "answer": frame.get("answer")}, sort_keys=True))
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

SEARCH = {
    # Wide enough to force branch ping-pong, lookahead rollouts (fork + trim)
    # and surviving-width forks; small budgets keep it fast.
    "text": {"threshold": 0.1, "beams": 4, "ahead": 2, "width": 2,
             "repetition": None, "diversity": None},
    "enum": {"threshold": 0.1, "width": 1},
    "branch": {"threshold": 0.1, "width": 1},
    "flow": {"threshold": 0.1, "width": 1},
    "queue": {"metric": "perplexity"},
}

#: Largest logprob difference tolerated between slot counts for a token both
#: produced. Observed worst case is 2.96e-03 on the tiny Q2_K model; this is
#: loose enough not to be flaky and tight enough that a real cache defect --
#: which would attach a prefix that is simply wrong -- blows straight past it.
LOGPROB_TOLERANCE = 1e-2


def _run_with_slots(slots, stl, model, syntax, search):
    env = dict(os.environ, AUTOCOG_KV_SLOTS=str(slots))
    r = subprocess.run(
        [sys.executable, "-c", RUNNER, stl, model, syntax, search],
        capture_output=True, text=True, timeout=600, env=env)
    assert r.returncode == 0, f"slots={slots} failed:\n{r.stderr[-2000:]}"
    return json.loads(r.stdout.strip().splitlines()[-1])


@pytest.fixture(scope="module")
def slot_runs(tmp_path_factory, llama3_model_path, syntax_path):
    tmp = tmp_path_factory.mktemp("kv")
    stl = tmp / "kv.stl"; stl.write_text(PROGRAM)
    search = tmp / "search.json"; search.write_text(json.dumps(SEARCH))
    args = (str(stl), llama3_model_path, syntax_path, str(search))
    runs = {n: _run_with_slots(n, *args) for n in (1, 4, 16)}
    runs["16-again"] = _run_with_slots(16, *args)
    return runs


@pytest.mark.timeout(900)
class TestSlotPool:
    def test_each_configuration_is_deterministic(self, slot_runs):
        """Whatever else varies, a given slot count must repeat itself, or
        nothing downstream is reproducible at all."""
        assert slot_runs[16] == slot_runs["16-again"]

    def test_shape_is_identical(self, slot_runs):
        """The cache may not change WHAT is evaluated: same actions, same
        order, same tree. This is the invariant the slot pool actually owes,
        and a recycling bug -- a branch resumed onto the wrong prefix -- would
        show up here as a different tree."""
        shapes = {n: [tuple(x) for x in r["shape"]]
                  for n, r in slot_runs.items()}
        ref = shapes[1]
        for n, s in shapes.items():
            assert s == ref, f"slots={n} evaluated a different tree"

    def test_constrained_answers_agree(self, slot_runs):
        """Free text may drift; a constrained choice may not. `select` ranks
        whole candidates that differ by far more than the numeric noise, so a
        disagreement here would mean the cache changed a decision, not a
        rounding."""
        answers = {n: r["answer"] for n, r in slot_runs.items()}
        assert len(set(answers.values())) == 1, answers

    def test_logprobs_agree_within_tolerance(self, slot_runs):
        """Where two configurations produced the same token, they must agree
        on its probability to within decode noise. This is what bounds the
        blast radius: the friction numbers move by this much and no more."""
        ref = slot_runs[1]
        worst, where = 0.0, None
        for n, run in slot_runs.items():
            for i, (t, l) in enumerate(zip(run["tokens"], run["logprobs"])):
                if t != ref["tokens"][i]:
                    continue        # a flipped near-tie; covered below
                for a, b in zip(l, ref["logprobs"][i]):
                    if abs(a - b) > worst:
                        worst, where = abs(a - b), (n, ref["shape"][i][1])
        assert worst <= LOGPROB_TOLERANCE, (
            f"logprob drift {worst:.2e} at {where}, over {LOGPROB_TOLERANCE}")

    def test_free_text_may_differ_and_only_free_text(self, slot_runs):
        """The documented non-guarantee, pinned so it stays documented.

        If this ever starts passing with zero differing nodes the backend has
        become reproducible across batchings and the note above -- and the
        campaign's need to pin slot count -- should be revisited. If a node
        OTHER than a free-text completion differs, the cache is changing
        constrained decisions and that is a defect.
        """
        ref = slot_runs[1]
        differing = set()
        for run in slot_runs.values():
            for i, t in enumerate(run["tokens"]):
                if t != ref["tokens"][i]:
                    differing.add(ref["shape"][i][1])
        assert differing, (
            "no configuration differed: if the backend is now reproducible "
            "across batchings, update this module's docstring and revisit "
            "whether campaigns still need to pin AUTOCOG_KV_SLOTS")
        for uid in differing:
            assert uid.startswith("field."), \
                f"a non-value action differed between slot counts: {uid}"
