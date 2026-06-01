#!/usr/bin/env python3
"""Guard against drift between the `test` dependency-group and the public extras.

PEP 735 dependency-groups and PEP 508 optional-dependencies (extras) are
separate namespaces: a group cannot `include-group` an extra, and resolving an
extra requires the build backend. So the runtime packages the test suite needs
(the `server` and `validate` extras) are *mirrored* into the `test` group in
pyproject.toml rather than referenced. Mirrors drift.

This check fails if any requirement in `[server]` or `[validate]` is not also
present (same name, same specifier) in the `test` group. It is intentionally
strict on the specifier string so that a version bump in one place that is not
mirrored in the other is caught, not silently accepted.

Run: python tests/scripts/check_dep_group_drift.py [path/to/pyproject.toml]
Exits 0 if in sync, 1 on drift. No third-party imports (uses tomllib + the
PEP 735 reference include-resolution algorithm inline).
"""

from __future__ import annotations

import re
import sys
import tomllib
from pathlib import Path

# Extras whose contents must be covered by the `test` group, because the test
# suite imports them (server endpoints, schema validation).
REQUIRED_EXTRAS = ("server", "validate")
GROUP = "test"


def _normalize(name: str) -> str:
    """PEP 503 name normalization (for comparing package names)."""
    return re.sub(r"[-_.]+", "-", name).lower()


def _req_key(spec: str) -> tuple[str, str]:
    """Split a PEP 508 specifier into (normalized name, normalized remainder).

    The remainder (version constraints, extras, markers) is whitespace-stripped
    so that "fastapi>=0.100" and "fastapi >= 0.100" compare equal, but a
    different bound does not.
    """
    s = spec.strip()
    # Name runs until the first version/extra/marker delimiter.
    m = re.match(r"^([A-Za-z0-9][A-Za-z0-9._-]*)", s)
    if not m:
        return (_normalize(s), "")
    name = m.group(1)
    remainder = s[len(name):]
    remainder = re.sub(r"\s+", "", remainder)
    return (_normalize(name), remainder)


def _resolve_group(groups: dict, name: str, seen: tuple = ()) -> list[str]:
    """PEP 735 reference include-resolution (cycle-safe), strings only out."""
    if name in seen:
        raise ValueError(f"Cyclic dependency group include: {name} via {seen}")
    if name not in groups:
        raise LookupError(f"Dependency group '{name}' not found")
    out: list[str] = []
    for item in groups[name]:
        if isinstance(item, str):
            out.append(item)
        elif isinstance(item, dict) and tuple(item.keys()) == ("include-group",):
            out.extend(_resolve_group(groups, _normalize(item["include-group"]), seen + (name,)))
        else:
            raise ValueError(f"Invalid dependency group item in '{name}': {item!r}")
    return out


def check(pyproject_path: Path) -> list[str]:
    """Return a list of human-readable drift problems (empty == in sync)."""
    data = tomllib.loads(pyproject_path.read_text())

    extras = data.get("project", {}).get("optional-dependencies", {})
    raw_groups = data.get("dependency-groups", {})
    groups = {_normalize(k): v for k, v in raw_groups.items()}

    problems: list[str] = []

    if _normalize(GROUP) not in groups:
        return [f"[dependency-groups] has no '{GROUP}' group"]

    group_specs = _resolve_group(groups, _normalize(GROUP))
    group_keys = {_req_key(s) for s in group_specs}

    for extra in REQUIRED_EXTRAS:
        if extra not in extras:
            problems.append(f"[project.optional-dependencies] has no '{extra}' extra")
            continue
        for spec in extras[extra]:
            key = _req_key(spec)
            if key not in group_keys:
                name, remainder = key
                # Distinguish "name missing entirely" from "specifier mismatch".
                same_name = [g for g in group_keys if g[0] == name]
                if same_name:
                    have = ", ".join(sorted(r for _, r in same_name)) or "(no constraint)"
                    problems.append(
                        f"extra '{extra}': '{spec}' has a different specifier in "
                        f"the '{GROUP}' group (group has '{name}{have}')"
                    )
                else:
                    problems.append(
                        f"extra '{extra}': '{spec}' is not mirrored in the '{GROUP}' group"
                    )
    return problems


def main(argv: list[str]) -> int:
    if len(argv) > 1:
        path = Path(argv[1])
    else:
        # Default: repo-root pyproject.toml (this file lives at tests/scripts/).
        path = Path(__file__).resolve().parents[2] / "pyproject.toml"

    if not path.is_file():
        print(f"error: {path} not found", file=sys.stderr)
        return 1

    problems = check(path)
    if problems:
        print(f"Dependency-group drift detected in {path}:", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        print(
            "\nThe `test` group must mirror the union of the "
            f"{', '.join(REQUIRED_EXTRAS)} extras. Update pyproject.toml so they agree.",
            file=sys.stderr,
        )
        return 1

    print(f"OK: '{GROUP}' group mirrors the {', '.join(REQUIRED_EXTRAS)} extras.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
