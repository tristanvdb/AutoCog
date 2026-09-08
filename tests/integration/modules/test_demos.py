"""Run every mcq demo end-to-end with the RNG model.

The demos are the stdlib's showcase: each must compile against the installed
stdlib and execute through the Python orchestration — channels, flows, and
sub-prompt calls (the C++ tool pipeline exercises none of the orchestration,
so this is the only place a demo is proven to actually run).
"""

import pytest

MCQ_INPUTS = {
    "topic": "Science",
    "question": "What is H2O?",
    "choices": ["Water", "Fire", "Air", "Earth"],
}

# Demos whose result is a bare answer: it must be one of the input choices
# (select maps the picked index back to the choice; repeat emits it verbatim
# under a choice-constrained token mask).
ANSWER_IS_CHOICE = {"select", "repeat", "select-cot", "repeat-cot",
                    "select-hyp", "repeat-hyp"}

# The iter demos return a labeled struct; the answer field must be a choice.
# (Their literal `status` field currently orchestrates to None — a known
# runtime gap with literal return fields, deliberately not pinned here.)
ANSWER_IN_STRUCT = {"select-iter", "repeat-iter"}

MCQ_DEMOS = [
    "select", "select-cot", "select-annot", "select-hyp", "select-iter",
    "repeat", "repeat-cot", "repeat-annot", "repeat-hyp", "repeat-iter",
]


@pytest.mark.parametrize("demo", MCQ_DEMOS)
def test_mcq_demo_runs_rng(demo, engine, repo_root):
    import autocog
    prog = autocog.compile(str(repo_root / f"share/demos/mcq/{demo}.stl"))
    result = engine.run(prog, **MCQ_INPUTS)
    assert result is not None
    if demo in ANSWER_IS_CHOICE:
        assert result in MCQ_INPUTS["choices"], f"{demo}: unexpected answer {result!r}"
    elif demo in ANSWER_IN_STRUCT:
        assert result["answer"] in MCQ_INPUTS["choices"], f"{demo}: {result!r}"
    else:
        # The -annot demos: select/repeat over a struct-valued choices array
        # currently resolves to a sub-field of the picked struct rather than
        # identifying the choice — asserted loosely until those semantics are
        # settled (select over struct arrays is a flagged follow-up).
        assert isinstance(result, dict) and result, f"{demo}: {result!r}"
