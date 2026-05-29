"""Tests for the ECS-flavored NDJSON log formatter (--json mode)."""

import json
import logging

import pytest

from autocog._json_formatter import ECSFormatter
from autocog.errors import (
    AutoCogError, CompileError, ConfigError, FileError, InternalError,
    OrchestrationError, SourceLocation,
)


def _render(formatter, exc=None, level=logging.INFO, msg="hello",
            args=(), name="autocog", extra=None):
    exc_info = (type(exc), exc, exc.__traceback__) if exc is not None else None
    record = logging.LogRecord(name, level, "", 0, msg, args, exc_info)
    if extra:
        record.__dict__.update(extra)
    return json.loads(formatter.format(record))


@pytest.fixture
def fmt():
    return ECSFormatter(service_version="0.6.0")


class TestUniversalFields:
    def test_one_valid_json_object(self, fmt):
        out = _render(fmt, msg="compiling foo.stl")
        # Output is a single object with the universal fields populated.
        assert out["log.logger"] == "autocog"
        assert out["message"] == "compiling foo.stl"
        assert out["service.name"] == "autocog"
        assert out["service.version"] == "0.6.0"

    def test_timestamp_is_utc_z(self, fmt):
        out = _render(fmt)
        assert out["@timestamp"].endswith("Z")
        # millisecond precision: ...:SS.mmmZ
        assert out["@timestamp"][-5] == "."

    @pytest.mark.parametrize("levelno,expected", [
        (5, "trace"), (10, "debug"), (20, "info"),
        (30, "warn"), (40, "error"), (50, "critical"),
    ])
    def test_level_mapping(self, fmt, levelno, expected):
        out = _render(fmt, level=levelno)
        assert out["log.level"] == expected

    def test_message_interpolation(self, fmt):
        out = _render(fmt, msg="loaded %d prompts", args=(3,))
        assert out["message"] == "loaded 3 prompts"


class TestExceptionFields:
    def _raise(self, exc):
        try:
            raise exc
        except type(exc) as e:
            return e

    def test_compile_error_with_location(self, fmt):
        e = self._raise(CompileError(
            "compilation failed",
            location=SourceLocation(file="x.stl", line=3, column=13),
        ))
        out = _render(fmt, exc=e, level=logging.ERROR, msg="%s", args=(e,))
        assert out["error.type"] == "CompileError"
        assert out["event.outcome"] == "failure"
        assert out["event.kind"] == "event"
        assert out["autocog.exception.recoverable"] is False
        assert out["autocog.exception.location.file"] == "x.stl"
        assert out["autocog.exception.location.line"] == 3
        assert out["autocog.exception.location.column"] == 13

    def test_file_error_path(self, fmt):
        e = self._raise(FileError("Cannot find file: x.stl", path="x.stl"))
        out = _render(fmt, exc=e, level=logging.ERROR, msg="%s", args=(e,))
        assert out["error.type"] == "FileError"
        assert out["autocog.exception.path"] == "x.stl"

    def test_orchestration_error_recoverable(self, fmt):
        e = self._raise(OrchestrationError("flow choice not found"))
        out = _render(fmt, exc=e, level=logging.ERROR, msg="%s", args=(e,))
        assert out["error.type"] == "OrchestrationError"
        assert out["autocog.exception.recoverable"] is True
        assert out["event.kind"] == "event"

    def test_internal_error_is_alert(self, fmt):
        e = self._raise(InternalError("Store: invalid ID 7"))
        out = _render(fmt, exc=e, level=logging.CRITICAL, msg="%s", args=(e,))
        assert out["error.type"] == "InternalError"
        assert out["event.kind"] == "alert"
        assert out["autocog.exception.recoverable"] is False


class TestBareException:
    """A non-AutoCog exception leaking from autocog code is an alert."""

    def test_runtime_error_surfaces_as_alert(self, fmt):
        try:
            raise RuntimeError("dictionary key error in resolve_channel")
        except RuntimeError as e:
            out = _render(fmt, exc=e, level=logging.CRITICAL, msg="%s", args=(e,))
        assert out["error.type"] == "RuntimeError"
        assert out["event.kind"] == "alert"
        assert out["event.outcome"] == "failure"
        # No autocog.exception.recoverable: it isn't an AutoCogError.
        assert "autocog.exception.recoverable" not in out


class TestExtraFields:
    def test_autocog_extra_flattened_to_dotted(self, fmt):
        out = _render(fmt, extra={"autocog_run_id": "abc123"})
        assert out["autocog.run.id"] == "abc123"
