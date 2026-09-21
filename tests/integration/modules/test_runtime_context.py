"""@extern and the ambient runtime context.

Two properties, and the second is the one that decides whether this is
usable at all: a marked entrypoint is required, and the context is
per-task rather than global — a campaign has samples under DIFFERENT
syntaxes in flight in one process.
"""

import asyncio
import json
import os

import pytest

from autocog.errors import ConfigError
from autocog.runtime import bind, context, extern, is_extern


@pytest.fixture(autouse=True)
def from_repo_root(repo_root, monkeypatch):
    monkeypatch.chdir(repo_root)


def test_the_decorator_does_not_wrap():
    """A resolver stays a plain function you can call from a test with no
    machinery. The decorator marks and declares; it does not intercept."""
    def plain(a, b):
        return a + b

    marked = extern(plain)
    assert marked is plain                       # same object, not a wrapper
    assert is_extern(marked) and not is_extern(lambda: None)
    assert marked(1, 2) == 3

    @extern(capabilities=["interrupt"])
    def guarded():
        return "ok"

    assert guarded() == "ok"
    assert getattr(guarded, "__autocog_extern__")["capabilities"] == ("interrupt",)


def test_an_undecorated_entrypoint_is_a_load_error(tmp_path, repo_root):
    """Mandatory, not conventional: the decorator is where a resolver
    declares what it may do beyond returning a value, and an undeclared
    capability is ambient authority. Enforced now, while the cost is a
    dozen edits rather than a migration."""
    import autocog
    from autocog.externals import load_externals

    (tmp_path / "bare.py").write_text("def resolve(**kwargs):\n    return 'x'\n")
    (tmp_path / "bare.stl").write_text(
        'from "bare.py" import resolve;\n\n'
        'prompt main {\n'
        '  is { question is text<length=20>; answer is text<length=10>; }\n'
        '  channel { question get question; }\n'
        '  return call resolve _;\n'
        '}\n\nexport main as main;\n')
    prog = autocog.compile(str(tmp_path / "bare.stl"))
    with pytest.raises(ConfigError, match="missing @autocog.runtime.extern"):
        load_externals(prog)


def test_reading_outside_a_run_raises():
    """No silent None. A resolver called directly must be passed what it
    needs as arguments — which is also why the decorator does not wrap."""
    with pytest.raises(ConfigError, match="only available while an entrypoint"):
        context.syntax.prompt_zero_index


def test_an_unknown_field_raises_rather_than_returning_none():
    with bind(syntax={"prompt_indent": "> "}):
        assert context.syntax.prompt_indent == "> "
        with pytest.raises(ConfigError, match="no field 'nope'"):
            context.syntax.nope


def test_reads_are_recorded():
    """The evidence for promoting an ambient read to a declared binding:
    a resolver reading syntax.prompt_zero_index is telling you its return
    value depends on the rendering."""
    with bind(prompt="main", syntax={"a": 1, "b": 2}) as ctx:
        context.syntax.a
        context.syntax.b
        context.syntax.a
        assert context.prompt == "main"
    assert ctx.reads == ["syntax.a", "syntax.b", "syntax.a"]


def test_the_context_is_per_task_not_global():
    """THE reason this is a ContextVar. A campaign runs an EnginePool per
    syntax in one process, with samples interleaved — module state would
    let a resolver under one syntax read another's, producing a plausible
    wrong answer with no error."""
    seen = {}

    async def one(name, value):
        with bind(syntax={"prompt_zero_index": value}):
            await asyncio.sleep(0)               # yield to the other task
            seen[name] = context.syntax.prompt_zero_index

    async def both():
        await asyncio.gather(one("zero", True), one("one", False))

    asyncio.run(both())
    assert seen == {"zero": True, "one": False}


def test_a_resolver_runs_inside_a_context(tmp_path, repo_root):
    """End to end, through a real program: the context is installed around
    the extern call and what it read lands on the recorded call."""
    import autocog
    from autocog.externals import load_externals
    from autocog.recorder import Recorder

    (tmp_path / "peek.py").write_text(
        "from autocog.runtime import context, extern\n\n\n"
        "@extern\n"
        "def resolve(**kwargs):\n"
        "    return f'indent={context.syntax.prompt_indent!r} "
        "prompt={context.prompt}'\n")
    (tmp_path / "peek.stl").write_text(
        'from "peek.py" import resolve;\n\n'
        'prompt main {\n'
        '  is { question is text<length=20>; answer is text<length=10>; }\n'
        '  channel { question get question; }\n'
        '  return call resolve _;\n'
        '}\n\nexport main as main;\n')

    prog = autocog.compile(str(tmp_path / "peek.stl"))
    engine = autocog.Engine(
        syntax=os.path.join(repo_root, "share", "syntax", "complete.json"),
        search=os.path.join(repo_root, "share", "search", "default.json"))
    rec = Recorder(kinds={"frame"}, path=str(tmp_path / "rec"))
    out = engine.run(prog, externals=load_externals(prog), recorder=rec,
                     question="What is H2O?")

    assert out == "indent='> ' prompt=main"
    trace = json.load(open(os.path.join(rec.path, "ctx-0.json")))
    call = trace["trace"][-1]["calls"][-1]
    assert call["callable"] == "resolve"
    assert call["context_reads"] == ["syntax.prompt_indent"]


def test_the_shipped_resolvers_are_all_decorated(repo_root):
    """Every entrypoint an shipped program names. A file added later
    without the decorator fails at load, but only for whoever runs that
    program — this is the sweep."""
    import autocog
    from autocog.externals import load_externals

    stlib = os.path.join(repo_root, "share", "library", "stlib")
    demos = os.path.join(repo_root, "share", "demos")
    names = set()
    for root, _, files in os.walk(demos):
        for f in sorted(files):
            if not f.endswith(".stl"):
                continue
            src = open(os.path.join(root, f)).read()
            if "export " not in src or ".py\" import" not in src:
                continue
            prog = autocog.compile(os.path.join(root, f), includes=[stlib])
            names |= set(load_externals(prog))      # raises if undecorated
    assert len(names) >= 8, sorted(names)
