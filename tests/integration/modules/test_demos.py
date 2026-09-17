"""Run every mcq demo end-to-end with the RNG model.

The demos are the stdlib's showcase: each must compile against the installed
stdlib and execute through the Python orchestration — channels, flows, and
sub-prompt calls (the C++ tool pipeline exercises none of the orchestration,
so this is the only place a demo is proven to actually run).

Two families, named for what the model is choosing among. `strings-*` take a
list of choice texts; `record-*` take labelled records, so the label is in the
data rather than something the harness prepends. Both return the choice that
was settled on — a mechanism that commits on something else (an index, a
letter) resolves it on its own return.
"""

import pytest

CHOICES = ["Water", "Fire", "Air", "Earth"]
LABELS = ["A", "B", "C", "D"]

STRINGS_INPUTS = {"topic": "Science", "question": "What is H2O?",
                  "choices": CHOICES}
RECORD_INPUTS = {"topic": "Science", "question": "What is H2O?",
                 "choices": [{"label": l, "value": v}
                             for l, v in zip(LABELS, CHOICES)]}

# The annot demos build their own per-choice records (value + an annotator's
# verdict), so they are fed plain strings like the strings-* family.
ANNOT = {"record-index-annot", "record-value-annot"}


def inputs_for(demo):
    return RECORD_INPUTS if demo.startswith("record-") and demo not in ANNOT \
        else STRINGS_INPUTS


# Demos whose result is a bare answer: it must be one of the choice texts.
ANSWER_IS_CHOICE = {"strings-select", "strings-repeat",
                    "strings-select-cot", "strings-repeat-cot",
                    "strings-select-hyp", "strings-repeat-hyp",
                    "record-index", "record-value", "record-label",
                    "record-index-annot"}

# The iter demos return a labeled struct; the answer field must be a choice.
# (Their literal `status` field currently orchestrates to None — a known
# runtime gap with literal return fields, deliberately not pinned here.)
ANSWER_IN_STRUCT = {"strings-select-iter", "strings-repeat-iter"}

MCQ_DEMOS = [
    "strings-select", "strings-select-cot", "strings-select-hyp",
    "strings-select-iter",
    "strings-repeat", "strings-repeat-cot",
    "strings-repeat-hyp", "strings-repeat-iter",
    "record-index", "record-value", "record-label",
    "record-index-annot",
    # record-value-annot needs choices[].value populated by the annotator
    # calls, but multi-field call returns currently collapse to a single
    # unnamed value (the literal-return-fields runtime gap), so the repeat
    # source correctly resolves to zero candidates.
    pytest.param("record-value-annot", marks=pytest.mark.xfail(
        reason="multi-field call returns do not populate struct arrays yet",
        strict=True)),
]


@pytest.mark.parametrize("demo", MCQ_DEMOS)
def test_mcq_demo_runs_rng(demo, engine, repo_root):
    import autocog
    prog = autocog.compile(str(repo_root / f"share/demos/mcq/{demo}.stl"))
    result = engine.run(prog, **inputs_for(demo))
    assert result is not None
    if demo in ANSWER_IS_CHOICE:
        assert result in CHOICES, f"{demo}: unexpected answer {result!r}"
    elif demo in ANSWER_IN_STRUCT:
        assert result["answer"] in CHOICES, f"{demo}: {result!r}"
    else:
        assert isinstance(result, dict) and result, f"{demo}: {result!r}"


@pytest.mark.parametrize("demo", ["strings-repeat-cot", "strings-select-iter"])
def test_mcq_demo_runs_real(demo, real_engine, repo_root):
    """Free-text-heavy demos on the real tokenizer.

    Regression for the UnicodeDecodeError crash: without the syntax's default
    completion vocab, a length-bounded field could end mid-token-stream inside
    a multi-byte UTF-8 sequence and the frame walk died decoding it. The
    ASCII default vocab makes that structurally impossible.
    """
    import autocog
    prog = autocog.compile(str(repo_root / f"share/demos/mcq/{demo}.stl"))
    result = real_engine.run(prog, **STRINGS_INPUTS)
    assert result is not None
