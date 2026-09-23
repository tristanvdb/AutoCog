"""Degradation ladder of autocog._schema.validate_artifact: every early-out
(no jsonschema, no metadata, no schema root, unknown format), the
referencing-free fallback validator, and the ConfigError surface."""

import sys

import pytest

from autocog import _schema
from autocog.errors import ConfigError


VIOLATING_STA = {"metadata": {"format": "sta"}}  # missing all required keys


def test_missing_metadata_and_unknown_format_are_skipped():
    assert _schema.validate_artifact({}) is None
    assert _schema.validate_artifact({"metadata": {}}) is None
    assert _schema.validate_artifact({"metadata": {"format": "no-such"}}) is None


def test_no_schema_root_skips(monkeypatch):
    monkeypatch.setattr(_schema, "SCHEMA_ROOT", None)
    assert _schema.validate_artifact(VIOLATING_STA) is None


def test_violation_raises_configerror():
    with pytest.raises(ConfigError, match="Schema violation in STA"):
        _schema.validate_artifact(VIOLATING_STA)


def test_missing_jsonschema_warns_once(monkeypatch, caplog):
    monkeypatch.setitem(sys.modules, "jsonschema", None)  # import -> ImportError
    monkeypatch.setattr(_schema, "_warned", False)
    with caplog.at_level("WARNING", logger="autocog"):
        assert _schema.validate_artifact(VIOLATING_STA) is None
        assert _schema.validate_artifact(VIOLATING_STA) is None
    warnings = [r for r in caplog.records if "jsonschema not installed" in r.message]
    assert len(warnings) == 1  # one-time warning


def test_missing_referencing_degrades_to_no_validation(monkeypatch):
    """Without `referencing` the cross-file $refs cannot resolve; validation
    is skipped entirely (same contract as missing jsonschema)."""
    monkeypatch.setitem(sys.modules, "referencing", None)
    assert _schema.validate_artifact(VIOLATING_STA) is None
