"""
AutoCog error hierarchy.

Three trees under one root:
  - CompileError: STL source errors (has source location)
  - ExecutionError: prompt instantiation, evaluation, parsing
  - InternalError: invariant violations (backtrace in Debug)
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

    def __init__(self, message):
        super().__init__(message)


# ============================================================================
# Compile-time tree
# ============================================================================

class CompileError(AutoCogError):
    """STL compilation error.

    `diagnostics` is the full list of Diagnostic records produced by the
    compile (errors, warnings, notes). `location` and `source_line` mirror the
    first error-level diagnostic for convenience.
    """
    def __init__(self, message, *, location=None,
                 source_line=None, notes=None, diagnostics=None):
        super().__init__(message)
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
    def __init__(self, message, *, path=None):
        super().__init__(message)
        self.path = path


class SchemaError(ExecutionError):
    """Serialized data failed schema/format validation.

    Raised when serialized content does not conform to its expected schema:
    an unknown discriminant tag, a missing required field, or a mistyped
    value. `path` locates the offending element (e.g. a JSON path or uid).
    """
    def __init__(self, message, *, path=None):
        super().__init__(message)
        self.path = path


class IntegrityError(ExecutionError):
    """Content-addressed artifact failed its hash check.

    Raised when a stored artifact's recomputed hash does not match the hash
    recorded in its metadata (corruption or tampering). `format` is the
    artifact format; `expected` is the stored hash; `actual` is the
    recomputed hash.
    """
    def __init__(self, message, *, format=None, expected=None, actual=None):
        super().__init__(message)
        self.format = format
        self.expected = expected
        self.actual = actual


class ModelError(ExecutionError):
    """Model loading, tokenization, or evaluation error."""
    def __init__(self, message, *, model_id=None, op=None):
        super().__init__(message)
        self.model_id = model_id
        self.op = op


class OrchestrationError(ExecutionError):
    """Prompt orchestration error (step, flow, channel resolution)."""

    def __init__(self, message, *, prompt=None, field=None):
        super().__init__(message)
        self.prompt = prompt
        self.field = field


class FlowInvariantError(OrchestrationError):
    """Flow choice invariant violation."""


class RemoteError(ExecutionError):
    """Remote server communication error."""

    def __init__(self, message, *, endpoint=None):
        super().__init__(message)
        self.endpoint = endpoint


class Timeout(ExecutionError, builtins.TimeoutError):
    """Operation timed out."""

    def __init__(self, message, *, seconds=None):
        super().__init__(message)
        self.seconds = seconds


class FileError(ExecutionError, builtins.OSError):
    """File not found or inaccessible."""
    def __init__(self, message, *, path=None):
        ExecutionError.__init__(self, message)
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

def handle(exc, log):
    """Handle an AutoCogError at a boundary (CLI or server). Returns an exit code."""
    if isinstance(exc, AutoCogError):
        if isinstance(exc, InternalError):
            log.critical("internal error: %s", exc)
            return 250
        log.error("%s", exc)
        return 1
    log.critical("uncaught: %s", exc, exc_info=True)
    return 251
