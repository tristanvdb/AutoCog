#!/usr/bin/env python3
"""Compare two sweep result files (NDJSON) config by config.

    python3 compare.py <baseline.ndjson> <new.ndjson>

Rows are matched on the autocog.bench.* parameters. Reports eval seconds,
restore/eval token split, and the speedup; a `!=` marker flags rows whose
eval-token counts differ (the two runs did different generation work there,
so the timing ratio is not a pure caching comparison).
"""

import json
import sys


def load(path):
    rows = {}
    for line in open(path):
        r = json.loads(line)
        key = tuple(sorted((k, v) for k, v in r.items()
                           if k.startswith("autocog.bench.") and not k.endswith(("wall_seconds", "model"))))
        rows[key] = r
    return rows


def main():
    base, new = load(sys.argv[1]), load(sys.argv[2])
    cols = ("beams", "ahead", "width")
    print(f"{'config':<24} {'base s':>8} {'new s':>8} {'speedup':>8}  "
          f"{'restore':>13} {'eval':>13}  same-work")
    for key in sorted(base):
        if key not in new:
            continue
        b, n = base[key], new[key]
        cfg = " ".join(f"{c}={b['autocog.bench.' + c]}" for c in cols)
        bs, ns = b["autocog.perf.advance_seconds"], n["autocog.perf.advance_seconds"]
        br, nr = b["autocog.perf.tokens.restore"], n["autocog.perf.tokens.restore"]
        be, ne = b["autocog.perf.tokens.eval"], n["autocog.perf.tokens.eval"]
        same = "yes" if be == ne else "!="
        print(f"{cfg:<24} {bs:8.1f} {ns:8.1f} {bs / ns:7.2f}x  "
              f"{br:>6}->{nr:<6} {be:>6}->{ne:<6}  {same}")


if __name__ == "__main__":
    main()
