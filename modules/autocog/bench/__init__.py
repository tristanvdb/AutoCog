"""autocog bench — reusable benchmarking harness.

Subcommands (wired in autocog.__main__):
    bench perf      computational cells (the benchmarks/compute semantics)
    bench quality   accuracy / token-overhead / friction (benchmarks/quality)
    bench campaign  a manifest of runs, executed in order

The drivers run against the worker abstraction (workers.LocalWorker for
now): an engine factory over one loaded model, so syntax sweeps do not
reload weights. Remote level-3 workers slot in behind the same surface.
"""

from .workers import LocalWorker
from .formatters import load_formatter

__all__ = ["LocalWorker", "load_formatter"]
