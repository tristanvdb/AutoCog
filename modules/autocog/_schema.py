"""Schema validation for AutoCog artifacts.

Validates artifacts against JSON Schemas in share/schemas/.
Gracefully degrades when jsonschema is not installed.
"""

import json
import logging
from pathlib import Path

_logger = logging.getLogger("autocog")
_warned = False
_schema_cache = {}


def _find_schema_root():
    """Locate share/schemas/ in installed package or dev layout."""
    pkg_dir = Path(__file__).resolve().parent
    # Installed layout: autocog/schemas/
    candidate = pkg_dir / "schemas"
    if candidate.is_dir():
        return candidate
    # Development layout: modules/autocog/ → repo root → share/schemas/
    repo = pkg_dir.parent.parent
    candidate = repo / "share" / "schemas"
    if candidate.is_dir():
        return candidate
    # CWD fallback (CI runs tests from the repo root)
    candidate = Path.cwd() / "share" / "schemas"
    if candidate.is_dir():
        return candidate
    return None


SCHEMA_ROOT = _find_schema_root()


def _get_validator():
    """Return jsonschema module or None with a one-time warning."""
    global _warned
    try:
        import jsonschema
        return jsonschema
    except ImportError:
        if not _warned:
            _logger.warning(
                "jsonschema not installed; skipping schema validation. "
                "Install with: pip install autocog[validate]. "
                "Or pass --no-schema-check to suppress this warning."
            )
            _warned = True
        return None


def _load_schema_store():
    if _schema_cache:
        return _schema_cache
    if not SCHEMA_ROOT or not SCHEMA_ROOT.exists():
        return _schema_cache
    for sf in SCHEMA_ROOT.rglob("*.schema.json"):
        s = json.loads(sf.read_text())
        sid = s.get("$id", sf.name)
        _schema_cache[sid] = s
    return _schema_cache


def _detect_format(artifact):
    """Detect artifact format from metadata or top-level keys."""
    fmt = artifact.get("metadata", {}).get("format")
    if fmt:
        return fmt
    if "prompts" in artifact and "entry_points" in artifact:
        if "defines" in artifact and "records" in artifact:
            return "ir"
        return "sta"
    if "actions" in artifact:
        return "fta"
    return None


def validate_artifact(artifact, filepath="<unknown>"):
    """Validate an artifact against its schema.

    Returns None on success, raises ConfigError on failure.
    Does nothing if jsonschema is not installed.
    """
    jsonschema = _get_validator()
    if jsonschema is None:
        return

    fmt = _detect_format(artifact)
    if not fmt:
        return  # can't detect format, skip

    if not SCHEMA_ROOT:
        return  # schemas not found

    schema_file = SCHEMA_ROOT / f"{fmt}.schema.json"
    if not schema_file.exists():
        return

    schema = json.loads(schema_file.read_text())
    store = _load_schema_store()

    try:
        from referencing import Registry, Resource
        from referencing.jsonschema import DRAFT202012
        registry = Registry().with_resources([
            (sid, Resource.from_contents(s, default_specification=DRAFT202012))
            for sid, s in store.items()
        ])
        validator = jsonschema.Draft202012Validator(schema, registry=registry)
        validator.validate(artifact)
    except jsonschema.ValidationError as e:
        from .errors import ConfigError
        path = ".".join(str(p) for p in e.absolute_path) or "$"
        raise ConfigError(
            f"Schema violation in {fmt.upper()} at $.{path}: {e.message}",
        )
    except ImportError:
        # referencing not installed, fall back to basic validation
        try:
            validator = jsonschema.Draft202012Validator(schema)
            validator.validate(artifact)
        except jsonschema.ValidationError as e:
            from .errors import ConfigError
            path = ".".join(str(p) for p in e.absolute_path) or "$"
            raise ConfigError(
                f"Schema violation in {fmt.upper()} at $.{path}: {e.message}",
            )


def compute_uid(artifact):
    """Compute the UID of a Python-side artifact dict.

    Strips metadata before hashing for stability across runs.
    Returns a 16-char hex string.
    """
    import copy
    import hashlib
    a = copy.deepcopy(artifact)
    a.pop("metadata", None)
    # Compact JSON with sorted keys (JCS-compatible for our types)
    canonical = json.dumps(a, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode()).hexdigest()[:16]
