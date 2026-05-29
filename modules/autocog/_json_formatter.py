"""ECS-flavored NDJSON log formatter.

Renders any logging.LogRecord as a single JSON object (one per line) using
Elastic Common Schema field names where they apply, plus an ``autocog.*``
vendor namespace for AutoCog-specific exception detail. Dotted-flat keys
(``"log.level"``, not nested) match what most SIEMs expect.

This formatter sees both Python ``logging`` records and C++ spdlog records
(the latter arrive via the bridge sink in python_sink.hxx), so a structured
``--json`` stream is unified across both layers.
"""

import json
import logging
from datetime import datetime, timezone


class ECSFormatter(logging.Formatter):
    """Emit log records as ECS-flavored NDJSON."""

    # Python logging level number -> ECS log.level string. spdlog's TRACE maps
    # to Python level 5 via the bridge sink; warn is 30 ("warning" in Python,
    # but ECS uses "warn").
    LEVEL_MAP = {
        5: "trace", 10: "debug", 20: "info",
        30: "warn", 40: "error", 50: "critical",
    }

    def __init__(self, service_version=None):
        super().__init__()
        if service_version is None:
            try:
                import autocog
                service_version = autocog.__version__
            except Exception:
                service_version = "unknown"
        self._service_version = service_version

    def format(self, record):
        ts = datetime.fromtimestamp(record.created, tz=timezone.utc)
        ts_str = ts.strftime("%Y-%m-%dT%H:%M:%S.") + f"{ts.microsecond // 1000:03d}Z"

        out = {
            "@timestamp": ts_str,
            "log.level": self.LEVEL_MAP.get(record.levelno, "info"),
            "log.logger": record.name,
            "message": record.getMessage(),
            "service.name": "autocog",
            "service.version": self._service_version,
        }

        if record.exc_info and record.exc_info[1] is not None:
            self._add_exception_fields(out, record.exc_info[1])

        # Callers may attach extra fields via logging's `extra=`:
        #   log.error("...", extra={"autocog_run_id": "abc"})
        # Keys beginning "autocog_" become dotted "autocog.*" ECS keys.
        for key, value in record.__dict__.items():
            if key.startswith("autocog_"):
                out["autocog." + key[len("autocog_"):].replace("_", ".")] = value

        return json.dumps(out, default=str)

    def _add_exception_fields(self, out, exc):
        from autocog import errors

        out["error.type"] = type(exc).__name__
        out["error.message"] = str(exc)
        out["event.outcome"] = "failure"

        if isinstance(exc, errors.AutoCogError):
            out["event.kind"] = (
                "alert" if isinstance(exc, errors.InternalError) else "event"
            )
            out["autocog.exception.recoverable"] = exc.recoverable

            if isinstance(exc, errors.OrchestrationError):
                if getattr(exc, "prompt", None):
                    out["autocog.exception.prompt"] = exc.prompt
                if getattr(exc, "field", None):
                    out["autocog.exception.field"] = exc.field
            if isinstance(exc, errors.ConfigError):
                if getattr(exc, "path", None):
                    out["autocog.exception.path"] = exc.path
            if isinstance(exc, errors.FileError):
                if getattr(exc, "path", None):
                    out["autocog.exception.path"] = exc.path
            if isinstance(exc, errors.ModelError):
                if getattr(exc, "model_id", None) is not None:
                    out["autocog.exception.model_id"] = exc.model_id
                if getattr(exc, "op", None):
                    out["autocog.exception.op"] = exc.op
            if isinstance(exc, errors.RemoteError):
                if getattr(exc, "endpoint", None):
                    out["autocog.exception.endpoint"] = exc.endpoint
            if isinstance(exc, errors.Timeout):
                if getattr(exc, "seconds", None):
                    out["autocog.exception.seconds"] = exc.seconds
            if isinstance(exc, errors.CompileError) and getattr(exc, "location", None):
                loc = exc.location
                # location is a SourceLocation dataclass (file/line/column).
                out["autocog.exception.location.file"] = loc.file
                out["autocog.exception.location.line"] = loc.line
                out["autocog.exception.location.column"] = loc.column
        else:
            # A non-AutoCog exception leaking from autocog code is a red flag:
            # surface it as an alert with its real type (e.g. "RuntimeError").
            out["event.kind"] = "alert"
