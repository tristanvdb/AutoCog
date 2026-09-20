"""The special-token syntax (share/syntax/special.json).

Structural markers are Llama-3 reserved special tokens written as their
surface strings; the tokenizer (special=true) collapses each to a single
reserved token id, so no schema or engine change is involved — the syntax is
pure configuration. The base model has never seen these tokens carry this
structure: it is *expected* to score them poorly. What these tests pin is
that the machinery is sound — the program renders, evaluates, and yields a
frame; every structural marker really is one token; and the completion
vocab keeps the model from emitting reserved tokens inside values.
"""

import pytest

PIN = '''
vocab digit = tokenize("0", "1", "2", "3", "4", "5", "6", "7", "8", "9");

prompt main {
  is {
    pin is text<length=4, vocab=digit, stop="">;
  }
  return {
    use pin;
  }
}

export main;
'''

MARKERS = "<|reserved_special_token_100|>"


@pytest.fixture
def special_engine(repo_root, search_path, llama3_model_path):
    import autocog
    return autocog.Engine(model=llama3_model_path,
                          syntax=str(repo_root / "share" / "syntax" / "special.json"),
                          search=search_path, n_ctx=2048)


def test_special_syntax_runs_and_constrains(special_engine, tmp_path):
    import autocog
    stl = tmp_path / "pin.stl"
    stl.write_text(PIN)
    prog = autocog.compile(str(stl))
    special_engine.set_seed(42)
    result = special_engine.run(prog)
    assert len(result) == 4 and result.isdigit(), repr(result)


def test_special_markers_are_single_tokens(special_engine, tmp_path):
    """Every field-separator node in the recorded FTT must hold exactly one
    token (the reserved token id >= 128000), not the ~10 ascii tokens the
    surface string would cost — the entire point of the syntax."""
    import autocog
    from autocog.recorder import Recorder
    stl = tmp_path / "pin.stl"
    stl.write_text(PIN)
    prog = autocog.compile(str(stl))
    special_engine.set_seed(42)
    rec = Recorder(kinds={"ftt"}, path=str(tmp_path / "rec"))
    special_engine.run(prog, recorder=rec)

    import json, pathlib
    ftts = list(pathlib.Path(rec.path).rglob("*.json"))
    seps = []

    def walk(node):
        if isinstance(node, dict):
            if str(node.get("uid", "")).startswith("endl."):
                seps.append(node["tokens"])
            for v in node.values():
                walk(v)
        elif isinstance(node, list):
            for v in node:
                walk(v)

    for f in ftts:
        walk(json.loads(f.read_text()))
    assert seps, "no separator nodes captured"
    for tokens in seps:
        assert len(tokens) == 1 and tokens[0] >= 128000, tokens
