#!/usr/bin/env python3
"""Check stlc error output against in-fixture expectations.

A negative (error) fixture declares the diagnostic(s) it expects with directive
comments:

    // expect-error: <substring>

The substring must appear in stlc's combined stdout+stderr. Multiple directives
may appear; all must match. A fixture with no directive falls back to requiring
a generic error/failed message (transition mode for un-annotated fixtures).

Regardless of directives, output containing "internal error" always FAILS: an
internal error is never an acceptable diagnostic.

Usage:
    stlc ... 2>&1 | python check_error_output.py <fixture.stl> <exit_code>

Exit codes:
    0  expectations satisfied
    1  expectation not met (missing substring, unexpected success, internal error)
    3  usage error
"""

import re
import sys

DIRECTIVE = re.compile(r"//\s*expect-error:\s*(.+?)\s*$")


def directives(fixture_path):
    out = []
    with open(fixture_path) as f:
        for line in f:
            m = DIRECTIVE.search(line)
            if m:
                out.append(m.group(1))
    return out


def main(argv):
    if len(argv) != 3:
        print("usage: check_error_output.py <fixture.stl> <exit_code>", file=sys.stderr)
        return 3
    fixture, exit_code = argv[1], argv[2]
    output = sys.stdin.read()

    # An internal error is never acceptable, even if a directive happens to match.
    if "internal error" in output.lower():
        print("FAIL: output contains 'internal error' (never acceptable)", file=sys.stderr)
        print(output, file=sys.stderr)
        return 1

    # The fixture must have been rejected.
    if exit_code == "0":
        print("FAIL: expected non-zero exit code, got 0", file=sys.stderr)
        print(output, file=sys.stderr)
        return 1

    expects = directives(fixture)
    if expects:
        missing = [e for e in expects if e not in output]
        if missing:
            for e in missing:
                print(f"FAIL: expected diagnostic substring not found: {e!r}", file=sys.stderr)
            print("--- actual output ---", file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        print(f"OK: matched {len(expects)} expected diagnostic(s)")
        return 0

    # Transition fallback: no directive -> require a generic error message.
    if re.search(r"error|failed", output, re.IGNORECASE):
        print("OK: rejected with a generic error message (no expect-error directive)")
        return 0
    print("FAIL: expected an error message in output", file=sys.stderr)
    print(output, file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
