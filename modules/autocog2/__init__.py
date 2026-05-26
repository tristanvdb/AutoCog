"""
AutoCog — Structured Thought Language runtime.

Usage:
    import autocog2 as autocog

    program = autocog.compile("program.stl", includes=["share/library"])
    engine = autocog.Engine(model="model.gguf", syntax="share/syntax/default.json")
    result = engine.run(program, topic="Science", question="2+2?")
"""

from .program import compile, Program
from .engine import Engine
from .context import Context
