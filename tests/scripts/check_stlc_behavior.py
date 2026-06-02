#!/usr/bin/env python3
"""Check stlc invocation behavior (exit code + diagnostics) for flag-driven tests.

Unlike check_error_output.py (which reads expectations from fixture content for
negative *programs*), this checks a specific stlc *invocation* against an
expected outcome supplied on the command line. Used for -D / argument handling
where the trigger is a flag, not the fixture.

Modes:
  ok                 expect exit 0, no error/warning required
  warning <substr>   expect exit 0 AND <substr> present (a non-fatal warning)
  error   <substr>   expect non-zero exit AND <substr> present

In all modes, output containing "internal error" FAILS (never acceptable).

Usage:
  stlc ... 2>&1 | check_stlc_behavior.py <exit_code> <mode> [substr]

Exit codes: 0 expectation met, 1 not met, 3 usage error.
"""

import sys


def main(argv):
    if len(argv) < 3:
        print("usage: check_stlc_behavior.py <exit_code> <mode> [substr]", file=sys.stderr)
        return 3
    exit_code, mode = argv[1], argv[2]
    substr = argv[3] if len(argv) > 3 else None
    output = sys.stdin.read()

    if "internal error" in output.lower():
        print("FAIL: output contains 'internal error' (never acceptable)", file=sys.stderr)
        print(output, file=sys.stderr)
        return 1

    nonzero = (exit_code != "0")

    if mode == "ok":
        if nonzero:
            print(f"FAIL: expected exit 0, got {exit_code}", file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        print("OK: clean exit")
        return 0

    if mode == "warning":
        if nonzero:
            print(f"FAIL: warning expected (exit 0), got exit {exit_code}", file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        if substr and substr not in output:
            print(f"FAIL: expected warning substring not found: {substr!r}", file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        print("OK: warning emitted, clean exit")
        return 0

    if mode == "error":
        if not nonzero:
            print("FAIL: error expected (non-zero exit), got exit 0", file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        if substr and substr not in output:
            print(f"FAIL: expected error substring not found: {substr!r}", file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        print("OK: rejected with expected diagnostic")
        return 0

    print(f"usage: unknown mode {mode!r}", file=sys.stderr)
    return 3


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
