#!/usr/bin/env python3
"""Golden file comparator with schema validation and structural diff.

Usage:
    # Compare actual output against golden
    python golden_compare.py actual.json golden.json

    # Validate a single file against its schema
    python golden_compare.py --validate-only file.json

    # Compare without schema validation (transition mode)
    python golden_compare.py --no-validate actual.json golden.json

Exit codes:
    0  match (or validation pass)
    1  structural mismatch
    2  schema validation failure
    3  usage error
"""

import argparse
import json
import sys
from pathlib import Path

import deepdiff

# ---------------------------------------------------------------------------
# Schema resolution
# ---------------------------------------------------------------------------

SCHEMA_ROOT = Path(__file__).resolve().parent.parent.parent / "share" / "schemas"

_schema_cache = {}

# When true, a missing jsonschema/referencing install is a hard error instead
# of a silent skip. Set from --force-validate so CI and the test suite can
# guarantee validation actually ran. The warning is emitted at most once.
_force_validate = False
_jsonschema_warned = False


def _try_import_jsonschema():
    """Return (jsonschema, Registry, Resource, DRAFT202012) or None.

    With --force-validate, a missing install exits the process; otherwise it
    warns once and returns None so the caller skips validation.
    """
    global _jsonschema_warned
    try:
        import jsonschema
        from referencing import Registry, Resource
        from referencing.jsonschema import DRAFT202012
        return (jsonschema, Registry, Resource, DRAFT202012)
    except ImportError as e:
        if _force_validate:
            print(f"ERROR: jsonschema/referencing required but not installed: {e}\n"
                  "Install with: pip install autocog[validate]", file=sys.stderr)
            sys.exit(2)
        if not _jsonschema_warned:
            print("WARNING: jsonschema not installed; skipping schema validation. "
                  "Install with: pip install autocog[validate], "
                  "or pass --force-validate to make this an error.",
                  file=sys.stderr)
            _jsonschema_warned = True
        return None


def _load_schema_store():
    if _schema_cache:
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
    # Heuristic for pre-metadata artifacts
    if "prompts" in artifact and "entry_points" in artifact:
        if "defines" in artifact and "records" in artifact:
            return "ir"
        return "sta"
    if "actions" in artifact:
        return "fta"
    return None


def validate(artifact, filepath="<unknown>"):
    """Validate artifact against its schema.

    Returns None on success, error message string on failure.
    """
    deps = _try_import_jsonschema()
    if deps is None:
        return None  # already warned (or exited under --force-validate)
    jsonschema, Registry, Resource, DRAFT202012 = deps

    fmt = _detect_format(artifact)
    if not fmt:
        return f"cannot detect format for {filepath}"

    schema_file = SCHEMA_ROOT / f"{fmt}.schema.json"
    if not schema_file.exists():
        return f"no schema for format '{fmt}'"

    schema = json.loads(schema_file.read_text())
    store = _load_schema_store()

    try:
        registry = Registry().with_resources([
            (sid, Resource.from_contents(s, default_specification=DRAFT202012))
            for sid, s in store.items()
        ])
        validator = jsonschema.Draft202012Validator(schema, registry=registry)
        validator.validate(artifact)
    except jsonschema.ValidationError as e:
        path = ".".join(str(p) for p in e.absolute_path) or "$"
        hint = _typo_hint(e, registry)
        msg = f"SCHEMA VIOLATION in {filepath}\n"
        msg += f"  at $.{path}\n"
        msg += f"  message: {e.message}\n"
        if hint:
            msg += f"  hint: {hint}\n"
        return msg

    return None


def _typo_hint(error, registry=None):
    """If the error looks like a typo'd field name, suggest the closest match.

    For `unevaluatedProperties` failures the offending field is reported against
    an inner subschema with no `properties` of its own -- the real field names
    live in $ref'd / allOf-composed parents. Walk those (resolving $refs via the
    registry) so the hint fires for composed schemas, not just flat ones.
    """
    if error.validator != "unevaluatedProperties" \
            and "unexpected" not in error.message.lower():
        return None

    import re
    m = re.search(r"'([^']+)' was unexpected", error.message)
    if not m:
        return None
    typo = m.group(1)

    expected = set()

    def collect(schema):
        if not isinstance(schema, dict):
            return
        for name in schema.get("properties", {}):
            expected.add(name)
        for sub in schema.get("allOf", []):
            ref = isinstance(sub, dict) and sub.get("$ref")
            if ref and registry is not None:
                try:
                    collect(registry.resolver().lookup(ref).contents)
                except Exception:
                    pass
            else:
                collect(sub)
        for sub in schema.get("anyOf", []) + schema.get("oneOf", []):
            collect(sub)

    if isinstance(error.schema, dict):
        collect(error.schema)
    parent = getattr(error, "parent", None)
    if parent is not None and isinstance(getattr(parent, "schema", None), dict):
        collect(parent.schema)

    for name in expected:
        if _levenshtein(typo, name) <= 2:
            return f"typo'd field name? Schema declares '{name}'."
    return None


def _levenshtein(a, b):
    if len(a) < len(b):
        return _levenshtein(b, a)
    if len(b) == 0:
        return len(a)
    prev = range(len(b) + 1)
    for i, ca in enumerate(a):
        curr = [i + 1]
        for j, cb in enumerate(b):
            curr.append(min(
                prev[j + 1] + 1,
                curr[j] + 1,
                prev[j] + (0 if ca == cb else 1)
            ))
        prev = curr
    return prev[-1]


# ---------------------------------------------------------------------------
# Structural comparison
# ---------------------------------------------------------------------------

# Paths stripped before comparison — these change between runs by design
EXCLUDE_PATHS = {
    "root['metadata']",
}


def compare(actual, golden, actual_path="<actual>", golden_path="<golden>"):
    """Structurally compare two artifacts, ignoring metadata.

    Returns None on match, a formatted diff message on mismatch.
    """
    diff = deepdiff.DeepDiff(
        golden, actual,
        exclude_paths=EXCLUDE_PATHS,
        ignore_order=False,
        verbose_level=2,
    )

    if not diff:
        return None

    return format_diff(diff, actual_path, golden_path)


def _count_changes(diff):
    """Count changes across the known deepdiff categories.

    Counting len(diff.values()) is fragile because categories are
    heterogeneous (dicts vs PrettyOrderedSets) and the set of categories
    varies across deepdiff versions. Sum only the categories we render.
    """
    total = 0
    for category in (
        "type_changes", "values_changed",
        "dictionary_item_added", "dictionary_item_removed",
        "iterable_item_added", "iterable_item_removed",
        "set_item_added", "set_item_removed",
    ):
        items = diff.get(category)
        if items:
            total += len(items)
    return total


def format_diff(diff, actual_path, golden_path):
    """Produce an agent/human-friendly diff message with JSONPaths."""
    lines = []

    total = _count_changes(diff)
    lines.append(f"GOLDEN MISMATCH: {total} difference(s)")
    lines.append(f"  actual: {actual_path}")
    lines.append(f"  golden: {golden_path}")

    # Type changes
    for path, change in diff.get("type_changes", {}).items():
        jp = _dd_to_jsonpath(path)
        lines.append(
            f"  type      {jp:50s} "
            f"actual={_short(change.get('new_value'))}  "
            f"golden={_short(change.get('old_value'))}"
        )

    # Value changes
    for path, change in diff.get("values_changed", {}).items():
        jp = _dd_to_jsonpath(path)
        lines.append(
            f"  changed   {jp:50s} "
            f"actual={_short(change.get('new_value'))}  "
            f"golden={_short(change.get('old_value'))}"
        )

    # Items added (in actual but not golden)
    for path, value in diff.get("dictionary_item_added", {}).items():
        jp = _dd_to_jsonpath(path)
        lines.append(
            f"  added     {jp:50s} "
            f"actual={_short(value)} (not in golden)"
        )

    for path, value in diff.get("iterable_item_added", {}).items():
        jp = _dd_to_jsonpath(path)
        lines.append(
            f"  added     {jp:50s} "
            f"actual={_short(value)} (not in golden)"
        )

    # Items removed (in golden but not actual)
    for path, value in diff.get("dictionary_item_removed", {}).items():
        jp = _dd_to_jsonpath(path)
        lines.append(
            f"  removed   {jp:50s} "
            f"golden={_short(value)} (not in actual)"
        )

    for path, value in diff.get("iterable_item_removed", {}).items():
        jp = _dd_to_jsonpath(path)
        lines.append(
            f"  removed   {jp:50s} "
            f"golden={_short(value)} (not in actual)"
        )

    return "\n".join(lines)


def _dd_to_jsonpath(dd_path):
    """Convert deepdiff root['a']['b'][0] to $.a.b[0]."""
    import re
    p = dd_path.replace("root", "$")
    p = re.sub(r"\['([^']+)'\]", r".\1", p)
    return p


def _short(value, max_len=40):
    """Short repr of a value for diff output."""
    s = json.dumps(value, default=str)
    if len(s) > max_len:
        return s[:max_len - 3] + "..."
    return s


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    global _force_validate
    parser = argparse.ArgumentParser(
        description="Compare golden files with schema validation")
    parser.add_argument("files", nargs="*",
                        help="actual.json golden.json (or single file with --validate-only)")
    parser.add_argument("--validate-only", action="store_true",
                        help="Validate file(s) against schema without comparison")
    parser.add_argument("--no-validate", action="store_true",
                        help="Skip schema validation")
    parser.add_argument("--force-validate", action="store_true",
                        help="Hard-require jsonschema; fail (exit 2) if it is not installed")
    args = parser.parse_args()

    # --no-validate (explicit skip) wins over --force-validate (explicit
    # require) if both are somehow passed.
    _force_validate = args.force_validate and not args.no_validate

    if args.validate_only:
        if not args.files:
            print("Error: --validate-only requires at least one file", file=sys.stderr)
            sys.exit(3)
        failed = False
        for filepath in args.files:
            data = json.loads(Path(filepath).read_text())
            err = validate(data, filepath)
            if err:
                print(err, file=sys.stderr)
                failed = True
        sys.exit(2 if failed else 0)

    if len(args.files) != 2:
        print("Error: expected exactly 2 files: actual.json golden.json", file=sys.stderr)
        sys.exit(3)

    actual_path, golden_path = args.files
    actual = json.loads(Path(actual_path).read_text())
    golden = json.loads(Path(golden_path).read_text())

    # Schema validation (both sides)
    if not args.no_validate:
        for data, fpath in [(actual, actual_path), (golden, golden_path)]:
            err = validate(data, fpath)
            if err:
                print(err, file=sys.stderr)
                sys.exit(2)

    # Structural comparison
    diff_msg = compare(actual, golden, actual_path, golden_path)
    if diff_msg:
        print(diff_msg, file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
