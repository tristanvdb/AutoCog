"""Tests for the ECS-flavored NDJSON log formatter (--json mode)."""

import json
import logging

import pytest

from autocog._json_formatter import ECSFormatter
from autocog.errors import (
    AutoCogError, CompileError, ConfigError, FileError, IntegrityError,
    InternalError, ModelError, OrchestrationError, RemoteError, SchemaError,
    SourceLocation, Timeout,
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
        assert out["autocog.exception.location.file"] == "x.stl"
        assert out["autocog.exception.location.line"] == 3
        assert out["autocog.exception.location.column"] == 13

    def test_file_error_path(self, fmt):
        e = self._raise(FileError("Cannot find file: x.stl", path="x.stl"))
        out = _render(fmt, exc=e, level=logging.ERROR, msg="%s", args=(e,))
        assert out["error.type"] == "FileError"
        assert out["autocog.exception.path"] == "x.stl"

    def test_orchestration_error(self, fmt):
        e = self._raise(OrchestrationError("flow choice not found"))
        out = _render(fmt, exc=e, level=logging.ERROR, msg="%s", args=(e,))
        assert out["error.type"] == "OrchestrationError"
        assert out["event.kind"] == "event"

    def test_internal_error_is_alert(self, fmt):
        e = self._raise(InternalError("Store: invalid ID 7"))
        out = _render(fmt, exc=e, level=logging.CRITICAL, msg="%s", args=(e,))
        assert out["error.type"] == "InternalError"
        assert out["event.kind"] == "alert"

    def test_orchestration_error_prompt_and_field(self, fmt):
        e = self._raise(OrchestrationError(
            "field not produced", prompt="ask", field="answer"))
        out = _render(fmt, exc=e, level=logging.ERROR, msg="%s", args=(e,))
        assert out["autocog.exception.prompt"] == "ask"
        assert out["autocog.exception.field"] == "answer"

    def test_config_error_path(self, fmt):
        e = self._raise(ConfigError("missing field", path="syntax.system"))
        out = _render(fmt, exc=e, level=logging.ERROR, msg="%s", args=(e,))
        assert out["error.type"] == "ConfigError"
        assert out["autocog.exception.path"] == "syntax.system"

    def test_schema_error_path(self, fmt):
        e = self._raise(SchemaError("schema mismatch", path="prompts.main"))
        out = _render(fmt, exc=e, level=logging.ERROR, msg="%s", args=(e,))
        assert out["error.type"] == "SchemaError"
        assert out["autocog.exception.path"] == "prompts.main"

    def test_integrity_error_fields(self, fmt):
        e = self._raise(IntegrityError(
            "checksum mismatch", format="json", expected="abc", actual="def"))
        out = _render(fmt, exc=e, level=logging.ERROR, msg="%s", args=(e,))
        assert out["error.type"] == "IntegrityError"
        assert out["autocog.exception.format"] == "json"
        assert out["autocog.exception.expected"] == "abc"
        assert out["autocog.exception.actual"] == "def"

    def test_model_error_fields(self, fmt):
        e = self._raise(ModelError("eval failed", model_id=2, op="evaluate"))
        out = _render(fmt, exc=e, level=logging.ERROR, msg="%s", args=(e,))
        assert out["error.type"] == "ModelError"
        assert out["autocog.exception.model_id"] == 2
        assert out["autocog.exception.op"] == "evaluate"

    def test_remote_error_endpoint(self, fmt):
        e = self._raise(RemoteError("connection refused", endpoint="http://h:8080"))
        out = _render(fmt, exc=e, level=logging.ERROR, msg="%s", args=(e,))
        assert out["error.type"] == "RemoteError"
        assert out["autocog.exception.endpoint"] == "http://h:8080"

    def test_timeout_seconds(self, fmt):
        e = self._raise(Timeout("deadline exceeded", seconds=30))
        out = _render(fmt, exc=e, level=logging.ERROR, msg="%s", args=(e,))
        assert out["error.type"] == "Timeout"
        assert out["autocog.exception.seconds"] == 30


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


class TestExtraFields:
    def test_autocog_extra_flattened_to_dotted(self, fmt):
        out = _render(fmt, extra={"autocog_run_id": "abc123"})
        assert out["autocog.run.id"] == "abc123"


class TestServiceVersion:
    def test_default_version_resolves_from_package(self):
        # No explicit version -> formatter reads autocog.__version__.
        import autocog
        out = _render(ECSFormatter())
        assert out["service.version"] == autocog.__version__
