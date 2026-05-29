"""
AutoCog error hierarchy.

Three trees under one root:
  - CompileError: STL source errors (has source location)
  - ExecutionError: prompt instantiation, evaluation, parsing
  - InternalError: invariant violations (backtrace in Debug)

Recovery is a per-exception flag with category defaults.
"""

import builtins
from dataclasses import dataclass, field


# ============================================================================
# Compile diagnostics
# ============================================================================

@dataclass
class SourceLocation:
    """A resolved position in STL source: file path, 1-based line and column."""
    file: str
    line: int
    column: int

    def __str__(self):
        return f"{self.file}:{self.line}:{self.column}"


@dataclass
class Diagnostic:
    """One compiler diagnostic (error, warning, or note).

    `level` is one of "error", "warning", "note". `location` is a
    SourceLocation or None (some diagnostics are not tied to a position).
    """
    level: str
    message: str
    location: "SourceLocation | None" = None
    source_line: "str | None" = None
    notes: list = field(default_factory=list)

    def __str__(self):
        prefix = f"{self.location}: " if self.location is not None else ""
        return f"{prefix}{self.level}: {self.message}"


class AutoCogError(Exception):
    """Root of the AutoCog error hierarchy."""
    recoverable_default = False

    def __init__(self, message, *, recoverable=None):
        super().__init__(message)
        self.recoverable = (
            self.recoverable_default if recoverable is None else recoverable
        )


# ============================================================================
# Compile-time tree
# ============================================================================

class CompileError(AutoCogError):
    """STL compilation error.

    `diagnostics` is the full list of Diagnostic records produced by the
    compile (errors, warnings, notes). `location` and `source_line` mirror the
    first error-level diagnostic for convenience.
    """
    def __init__(self, message, *, recoverable=None, location=None,
                 source_line=None, notes=None, diagnostics=None):
        super().__init__(message, recoverable=recoverable)
        self.location = location
        self.source_line = source_line
        self.notes = notes or []
        self.diagnostics = diagnostics or []


class ParseError(CompileError):
    """STL parse error."""
    pass


# ============================================================================
# Execution tree
# ============================================================================

class ExecutionError(AutoCogError):
    """Error during prompt instantiation, evaluation, or parsing."""
    pass


class ConfigError(ExecutionError):
    """Configuration or file loading error."""
    def __init__(self, message, *, recoverable=None, path=None):
        super().__init__(message, recoverable=recoverable)
        self.path = path


class ModelError(ExecutionError):
    """Model loading, tokenization, or evaluation error."""
    def __init__(self, message, *, recoverable=None, model_id=None, op=None):
        super().__init__(message, recoverable=recoverable)
        self.model_id = model_id
        self.op = op


class OrchestrationError(ExecutionError):
    """Prompt orchestration error (step, flow, channel resolution)."""
    recoverable_default = True

    def __init__(self, message, *, recoverable=None, prompt=None, field=None):
        super().__init__(message, recoverable=recoverable)
        self.prompt = prompt
        self.field = field


class FlowInvariantError(OrchestrationError):
    """Flow choice invariant violation."""
    recoverable_default = False


class RemoteError(ExecutionError):
    """Remote server communication error."""
    recoverable_default = True

    def __init__(self, message, *, recoverable=None, endpoint=None):
        super().__init__(message, recoverable=recoverable)
        self.endpoint = endpoint


class Timeout(ExecutionError, builtins.TimeoutError):
    """Operation timed out."""
    recoverable_default = True

    def __init__(self, message, *, recoverable=None, seconds=None):
        super().__init__(message, recoverable=recoverable)
        self.seconds = seconds


class FileError(ExecutionError, builtins.OSError):
    """File not found or inaccessible."""
    def __init__(self, message, *, recoverable=None, path=None):
        ExecutionError.__init__(self, message, recoverable=recoverable)
        self.path = path


# ============================================================================
# Not implemented (pre-1.0 gaps)
# ============================================================================

class NotImplementedError(AutoCogError, builtins.NotImplementedError):
    """Feature not yet implemented. Slated for removal before 1.0."""
    pass


# ============================================================================
# Internal errors (bugs / invariant violations)
# ============================================================================

class InternalError(AutoCogError):
    """Internal invariant violation. Indicates a bug."""
    pass


# ============================================================================
# Recovery helper
# ============================================================================

def handle(exc, log, *, allow_recovery=False):
    """Handle an AutoCogError at a boundary (CLI or server).

    Returns (exit_code, recovered).
    """
    if isinstance(exc, AutoCogError):
        if isinstance(exc, InternalError):
            log.critical("internal error: %s", exc)
            return (250, False)
        log.error("%s", exc)
        if allow_recovery and exc.recoverable:
            return (0, True)
        return (1, False)
    log.critical("uncaught: %s", exc, exc_info=True)
    return (251, False)
