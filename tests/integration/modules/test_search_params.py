"""P0 search-parameter plumbing: the registry-backed resolver, the
choice-scoring triple (ranking.metric / threshold.{metric,value}), and the
argument bridge (programs declare tunable search via arguments; callers
override them with compile-time defines).
"""

import json

import pytest

import autocog
from autocog.errors import CompileError
from autocog.runtime.sta import runtime_sta_cxx


@pytest.fixture(autouse=True)
def from_repo_root(repo_root, monkeypatch):
    monkeypatch.chdir(repo_root)


# The argument bridge: a FILE-scope argument is the caller-tunable knob
# (-D / compile defines). Prompt-scope arguments are instantiation
# parameters (`main<beams=8>`), a separate mechanism.
BRIDGE_STL = """
argument beams = 4;

prompt main {
  is { q is text<length=8>; }
  search { text.beams is beams; }
  channel { q get q; }
  return { use q; }
}
"""

TRIPLE_STL = """
prompt main {
  is {
    q is text<length=8>;
    a is enum("yes", "no");
  }
  search {
    enum.ranking.metric is "sum";
    enum.threshold.value is 0.05;
  }
  channel { q get q; }
  return { use a; }
}
"""


def instantiate_fta(repo_root, program, content):
    syntax_id = runtime_sta_cxx.load_syntax(
        str(repo_root / "share" / "syntax" / "complete.json"))
    search_id = runtime_sta_cxx.load_search(
        str(repo_root / "share" / "search" / "default.json"))
    fta_id = runtime_sta_cxx.instantiate(program.id, "main", content,
                                         syntax_id, search_id)
    try:
        return json.loads(runtime_sta_cxx.dump_fta(fta_id))
    finally:
        runtime_sta_cxx.release_fta(fta_id)


def action_of(fta, kind):
    return next(a for a in fta["actions"] if a["type"] == kind)


def test_argument_bridge(repo_root, tmp_path):
    stl = tmp_path / "bridge.stl"
    stl.write_text(BRIDGE_STL)

    prog = autocog.compile(str(stl))
    fta = instantiate_fta(repo_root, prog, {})
    assert action_of(fta, "complete")["beams"] == 4

    prog8 = autocog.compile(str(stl), defines={"beams": 8})
    fta8 = instantiate_fta(repo_root, prog8, {})
    assert action_of(fta8, "complete")["beams"] == 8


def test_choice_triple_reaches_fta(repo_root, tmp_path):
    stl = tmp_path / "triple.stl"
    stl.write_text(TRIPLE_STL)

    prog = autocog.compile(str(stl))
    fta = instantiate_fta(repo_root, prog, {"q": "hello"})
    choose = action_of(fta, "choose")
    assert choose["ranking"] == "sum"
    assert choose["threshold"] == pytest.approx(0.05)
    # threshold.metric stays default -> not emitted (wire stability).
    assert "threshold.metric" not in choose


def test_default_program_wire_is_stable(repo_root, tmp_path):
    stl = tmp_path / "plain.stl"
    stl.write_text(TRIPLE_STL.replace('    enum.ranking.metric is "sum";\n', "")
                             .replace('    enum.threshold.value is 0.05;\n', ""))
    prog = autocog.compile(str(stl))
    fta = instantiate_fta(repo_root, prog, {"q": "hello"})
    choose = action_of(fta, "choose")
    assert "ranking" not in choose
    assert "threshold.metric" not in choose


def test_unknown_param_is_compile_error(tmp_path):
    stl = tmp_path / "typo.stl"
    stl.write_text(BRIDGE_STL.replace("text.beams is beams;",
                                      "text.beems is beams;"))
    with pytest.raises(CompileError):
        autocog.compile(str(stl))


def test_sum_ranking_prefers_short_choice_on_rng(repo_root, tmp_path):
    """Behavioral: under rng logits the joint probability shrinks with token
    count, so `sum` ranking must select the shortest choice; `mean` has no
    such length bias (its pick is seed-dependent but well-defined)."""
    from autocog.engine import Engine

    choices = ["tiny", "a very very very long choice answer text indeed"]
    search = json.load(open(repo_root / "share" / "search" / "default.json"))
    search["enum"]["ranking"] = {"metric": "sum"}
    search_file = tmp_path / "sum.json"
    search_file.write_text(json.dumps(search))

    prog = autocog.compile(str(repo_root / "share" / "demos" / "mcq" / "repeat.stl"),
                           includes=[str(repo_root / "share" / "demos" / "mcq")])

    engine = Engine(model=None,
                    syntax=str(repo_root / "share" / "syntax" / "complete.json"),
                    search=str(search_file))
    engine.set_seed(42)
    result = engine.run(prog, topic="t", question="pick one", choices=choices)
    answer = result if isinstance(result, str) else result.get("answer")
    assert answer == "tiny"


def test_empty_choices_is_user_error(repo_root):
    """C5: running a choice program with zero candidates fails with a
    user-attributable message (the array-range guard), never the backend's
    former InternalError('Choice action has no choices') — which is now a
    ConfigError naming the action, kept as defense-in-depth for choice
    sources that bypass ranged arrays. (Instantiation itself stays
    permissive: the ista tooling inspects FTAs without content.)"""
    from autocog.engine import Engine
    from autocog.errors import AutoCogError

    prog = autocog.compile(str(repo_root / "share" / "demos" / "mcq" / "select.stl"),
                           includes=[str(repo_root / "share" / "demos" / "mcq")])
    engine = Engine(model=None,
                    syntax=str(repo_root / "share" / "syntax" / "complete.json"),
                    search=str(repo_root / "share" / "search" / "default.json"))
    with pytest.raises(AutoCogError, match="requires at least"):
        engine.run(prog, topic="t", question="q", choices=[])


STOP_STL = """
argument max_terms = 5;

prompt main {
  is { q is text<length=8>; }
  search {
    queue.stop is ((__status__.tree.terminals >= max_terms)
                || (__status__.best.proba >= 0.9));
  }
  channel { q get q; }
  return { use q; }
}
"""


def test_queue_stop_translation(repo_root, tmp_path):
    """queue.stop written in the expression grammar reaches the FTA as the
    translated termination predicate; file-scope arguments fold into the
    comparison constants (-D overrides them)."""
    stl = tmp_path / "stop.stl"
    stl.write_text(STOP_STL)

    fta = instantiate_fta(repo_root, autocog.compile(str(stl)), {"q": "x"})
    stop = fta["queue"]["stop"]
    assert stop == {"any": [{"ge": ["terminals", 5.0]},
                            {"ge": ["best.proba", pytest.approx(0.9)]}]}

    fta8 = instantiate_fta(repo_root,
                           autocog.compile(str(stl), defines={"max_terms": 8}),
                           {"q": "x"})
    assert fta8["queue"]["stop"]["any"][0] == {"ge": ["terminals", 8.0]}
