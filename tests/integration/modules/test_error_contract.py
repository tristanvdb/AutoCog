"""
Error contract tests — verify every typed AutoCog error reaches Python with
the right type and structured fields, and that predictable failures never leak
a bare RuntimeError from autocog.* code.
"""

import pytest

import autocog
from autocog.errors import (
    AutoCogError, ExecutionError, CompileError, ConfigError, ModelError,
    OrchestrationError, FlowInvariantError, RemoteError, Timeout, FileError,
    InternalError, Diagnostic, SourceLocation, SchemaError, IntegrityError,
)


class TestTypedErrors:
    """Each predictable failure raises the expected type with structured fields."""

    def test_missing_file_raises_file_error(self):
        """A missing top-level input is a FileError carrying the path."""
        with pytest.raises(FileError) as ei:
            autocog.compile("/nonexistent/path.stl")
        assert ei.value.path == "/nonexistent/path.stl"
        # FileError is also an OSError (dual inheritance).
        assert isinstance(ei.value, OSError)

    def test_compile_error_is_autocog_error(self, repo_root):
        """Malformed STL raises CompileError, which is an AutoCogError."""
        bad = repo_root / "tests/fixtures/stl/errors/test_syntax_error.stl"
        with pytest.raises(AutoCogError) as ei:
            autocog.compile(str(bad))
        assert isinstance(ei.value, CompileError)

    def test_compile_error_carries_diagnostics(self, repo_root):
        """CompileError carries the full structured diagnostics list."""
        bad = repo_root / "tests/fixtures/stl/errors/test_missing_semicolon.stl"
        with pytest.raises(CompileError) as ei:
            autocog.compile(str(bad))
        diags = ei.value.diagnostics
        assert diags
        assert all(isinstance(d, Diagnostic) for d in diags)
        assert any(d.level == "error" for d in diags)

    def test_compile_error_carries_location(self, repo_root):
        """An error-level diagnostic carries a resolved SourceLocation."""
        bad = repo_root / "tests/fixtures/stl/errors/test_missing_semicolon.stl"
        with pytest.raises(CompileError) as ei:
            autocog.compile(str(bad))
        loc = ei.value.location
        assert isinstance(loc, SourceLocation)
        assert loc.file.endswith(".stl")
        assert isinstance(loc.line, int) and loc.line > 0
        assert isinstance(loc.column, int) and loc.column > 0

    def test_unresolved_import_is_located_compile_error(self, repo_root):
        """A missing *imported* file is a CompileError located at the import."""
        bad = repo_root / "tests/fixtures/stl/errors/test_undefined_import.stl"
        with pytest.raises(CompileError) as ei:
            autocog.compile(str(bad))
        # The unresolved-import diagnostic should be present and located in the
        # importing file (not the missing one).
        located = [d for d in ei.value.diagnostics
                   if d.level == "error" and d.location is not None]
        assert located, "expected at least one located error diagnostic"


class TestNoRawRuntimeError:
    """Predictable bad input fails as an AutoCogError, never a bare exception."""

    @pytest.mark.parametrize("scenario,arg", [
        ("missing_file",   "/no/such/file.stl"),
        ("malformed_stl",  "tests/fixtures/stl/errors/test_syntax_error.stl"),
        ("unexpected_tok", "tests/fixtures/stl/errors/test_unexpected_token.stl"),
        ("undefined_rec",  "tests/fixtures/stl/errors/test_undefined_record.stl"),
        ("undefined_imp",  "tests/fixtures/stl/errors/test_undefined_import.stl"),
    ])
    def test_compile_failures_are_typed(self, scenario, arg, repo_root):
        path = arg if arg.startswith("/") else str(repo_root / arg)
        with pytest.raises(Exception) as ei:
            autocog.compile(path)
        assert isinstance(ei.value, AutoCogError), (
            f"scenario '{scenario}' raised {type(ei.value).__name__}, "
            f"not an AutoCogError; predictable failures must be typed."
        )
        # And specifically not a bare RuntimeError leaking from C++/bindings.
        assert not (type(ei.value) is RuntimeError)


class TestSchemaAndIntegrityErrors:
    """SchemaError and IntegrityError sit in the execution tree and carry the
    structured fields the C++ translator forwards across the boundary."""

    def test_schema_error_hierarchy_and_fields(self):
        assert issubclass(SchemaError, ExecutionError)
        assert issubclass(SchemaError, AutoCogError)
        e = SchemaError("bad tag", path="actions[0].type")
        assert e.path == "actions[0].type"
        assert str(e) == "bad tag"

    def test_integrity_error_hierarchy_and_fields(self):
        assert issubclass(IntegrityError, ExecutionError)
        assert issubclass(IntegrityError, AutoCogError)
        e = IntegrityError("hash mismatch", format="fta", expected="aa", actual="bb")
        assert (e.format, e.expected, e.actual) == ("fta", "aa", "bb")
        assert str(e) == "hash mismatch"

    def test_distinct_from_config_error(self):
        # SchemaError must not be a ConfigError (different semantic category).
        assert not issubclass(SchemaError, ConfigError)
        assert not issubclass(IntegrityError, ConfigError)
