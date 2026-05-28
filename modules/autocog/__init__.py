"""
AutoCog — Structured Thought Language runtime.

Usage:
    import autocog

    program = autocog.compile("program.stl", includes=["share/library"])
    engine = autocog.Engine(model="model.gguf", syntax="share/syntax/default.json")
    result = engine.run(program, topic="Science", question="2+2?")
"""

import logging as _logging

# Register custom TRACE level (below DEBUG)
TRACE = 5
_logging.addLevelName(TRACE, "TRACE")
def _trace(self, msg, *args, **kw):
    if self.isEnabledFor(TRACE):
        self._log(TRACE, msg, args, **kw)
_logging.Logger.trace = _trace

from importlib.metadata import version as _pkg_version
try:
    __version__ = _pkg_version("autocog")
except Exception:
    __version__ = "unknown"

from .program import compile, load, Program
from .engine import Engine
from .context import Context
from .remote import RemoteEngine, remote_run


def set_log_level(level):
    """Set log level for both Python and C++ layers.

    Args:
        level: Python logging level (e.g. logging.DEBUG, autocog.TRACE)
    """
    logger = _logging.getLogger("autocog")
    logger.setLevel(level)
    # The shared store library holds the single C++ logger
    from .runtime.sta import runtime_sta_cxx
    runtime_sta_cxx.set_log_level(level)
