"""
AutoCog — Structured Thought Language runtime.

Usage:
    import autocog

    program = autocog.compile("program.stl", includes=["share/library"])
    engine = autocog.Engine(model="model.gguf", syntax="share/syntax/default.json")
    result = engine.run(program, topic="Science", question="2+2?")
"""

from importlib.metadata import version as _pkg_version
try:
    __version__ = _pkg_version("autocog")
except Exception:
    __version__ = "unknown"

from .program import compile, load, Program
from .engine import Engine
from .context import Context
