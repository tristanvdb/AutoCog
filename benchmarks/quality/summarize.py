#!/usr/bin/env python3
"""Cross-model summary of quality-benchmark results.

    python3 summarize.py results1.ndjson results2.ndjson ... [--ref MODEL]

Builds the model x (syntax, demo) accuracy matrix from any number of run.py
result files, plus a delta view against a reference model (default: the
first model seen, sorted). The point is RELATIVE accuracy — how much a
pattern extracts from a base model vs its instruct sibling, and how the
gap moves with scale — so absolute numbers are printed but the delta table
is the product.
"""

import argparse
import collections
import json


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+", help="run.py result ndjson files")
    ap.add_argument("--ref", default="", help="reference model for the delta view")
    args = ap.parse_args()

    # (model, syntax, demo) -> [correct, total]
    acc = collections.defaultdict(lambda: [0, 0])
    for path in args.files:
        for line in open(path):
            r = json.loads(line)
            if r.get("event.action") != "quality.run":
                continue
            key = (r["autocog.bench.model"], r["autocog.bench.syntax"], r["autocog.bench.demo"])
            acc[key][1] += 1
            acc[key][0] += bool(r["autocog.bench.correct"])

    models = sorted({m for m, _, _ in acc})
    patterns = sorted({(s, d) for _, s, d in acc})
    if not models:
        raise SystemExit("no bench.question records found")
    ref = args.ref or models[0]
    if ref not in models:
        raise SystemExit(f"reference model {ref!r} not in results ({models})")

    def cell(m, p):
        c, n = acc.get((m,) + p, [0, 0])
        return c / n if n else None

    header = "| model | " + " | ".join(f"{s}/{d}" for s, d in patterns) + " |"
    print("# Quality summary — accuracy by model x pattern\n")
    print(header)
    print("|" + "---|" * (len(patterns) + 1))
    for m in models:
        row = [f"{cell(m, p):.0%} (n={acc[(m,)+p][1]})" if cell(m, p) is not None else "-"
               for p in patterns]
        print(f"| {m} | " + " | ".join(row) + " |")

    print(f"\n## Delta vs {ref} (percentage points)\n")
    print(header)
    print("|" + "---|" * (len(patterns) + 1))
    for m in models:
        if m == ref:
            continue
        row = []
        for p in patterns:
            a, b = cell(m, p), cell(ref, p)
            row.append(f"{100 * (a - b):+.0f}" if a is not None and b is not None else "-")
        print(f"| {m} | " + " | ".join(row) + " |")
    print("\n(positive = model beats the reference on that pattern; "
          "chance is 25% on 4-choice questions)")


if __name__ == "__main__":
    main()
