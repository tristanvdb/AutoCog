#!/usr/bin/env python3
"""Plot compute-benchmark result files against each other.

    python3 plot.py --out DIR label1=results-a.ndjson label2=results-b.ndjson ...

Each labeled series is one sweep (an era of the backend, a machine, a
model...). Rows are matched on (beams, ahead, width). Four figures:

    wall.png        evaluation seconds per configuration (log scale)
    restore.png     restore share of decoded tokens — the KV-caching story
    pertoken.png    milliseconds per decoded token — the batching story
    work.png        productive (eval) tokens — whether runs did the same work
    productive.png  milliseconds per *productive* token — the bottom line:
                    total cost divided by useful output, so it stays
                    comparable across series even when the amount of work
                    changed (restore waste and per-call overhead both count
                    against the numerator)

Configurations where eval-token counts differ across series are marked *
in wall.png: their timing ratios include a change of work, not just of
machinery (e.g. the forced-scoring fix changed real-model exploration).
"""

import argparse
import json
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def load(path):
    rows = {}
    for line in open(path):
        r = json.loads(line)
        try:
            key = (r["autocog.bench.beams"], r["autocog.bench.ahead"], r["autocog.bench.width"])
        except KeyError:
            continue
        rows[key] = {
            "seconds": r["autocog.perf.advance_seconds"],
            "restore": r["autocog.perf.tokens.restore"],
            "eval": r["autocog.perf.tokens.eval"],
        }
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("series", nargs="+", help="label=path.ndjson")
    ap.add_argument("--out", default=".")
    ap.add_argument("--title", default="")
    args = ap.parse_args()

    data = {}
    for spec in args.series:
        label, path = spec.split("=", 1)
        data[label] = load(path)
    labels = list(data)

    keys = sorted(set().union(*[set(d) for d in data.values()]))
    names = [f"b{b} a{a} w{w}" for b, a, w in keys]
    x = range(len(keys))
    width = 0.8 / len(labels)
    colors = plt.cm.viridis([i / max(1, len(labels) - 1 or 1) for i in range(len(labels))])

    # Configs where the series did different amounts of productive work.
    def evals(k):
        return {d.get(k, {}).get("eval") for d in data.values() if k in d}
    changed = {k for k in keys if len(evals(k)) > 1}

    os.makedirs(args.out, exist_ok=True)

    def grouped_bars(value_fn, ylabel, fname, log=False, mark_changed=False, pct=False):
        fig, ax = plt.subplots(figsize=(max(8, len(keys) * 0.6), 4.5))
        for i, label in enumerate(labels):
            xs = [xi + i * width for xi in x]
            ys = [value_fn(data[label].get(k)) for k in keys]
            ax.bar(xs, [y if y is not None else 0 for y in ys], width,
                   label=label, color=colors[i])
        ticks = [xi + width * (len(labels) - 1) / 2 for xi in x]
        tick_names = [n + (" *" if mark_changed and k in changed else "")
                      for n, k in zip(names, keys)]
        ax.set_xticks(ticks, tick_names, rotation=60, ha="right", fontsize=8)
        ax.set_ylabel(ylabel)
        if log:
            ax.set_yscale("log")
        if pct:
            ax.set_ylim(0, 100)
        title = args.title or "compute benchmark"
        if mark_changed and changed:
            title += "   (* = series did different work: compare shape, not ratio)"
        ax.set_title(title, fontsize=10)
        ax.legend(fontsize=9)
        ax.grid(axis="y", alpha=0.3)
        fig.tight_layout()
        fig.savefig(os.path.join(args.out, fname), dpi=130)
        plt.close(fig)

    grouped_bars(lambda r: r and r["seconds"],
                 "evaluation seconds (log)", "wall.png", log=True, mark_changed=True)
    grouped_bars(lambda r: r and (100.0 * r["restore"] / max(1, r["restore"] + r["eval"])),
                 "restore share of decoded tokens (%)", "restore.png", pct=True)
    grouped_bars(lambda r: r and (1000.0 * r["seconds"] / max(1, r["restore"] + r["eval"])),
                 "ms per decoded token", "pertoken.png")
    grouped_bars(lambda r: r and r["eval"],
                 "productive (eval) tokens (log)", "work.png", log=True)
    grouped_bars(lambda r: r and (1000.0 * r["seconds"] / max(1, r["eval"])),
                 "ms per productive token", "productive.png", log=True)

    print(f"wrote wall.png restore.png pertoken.png work.png productive.png -> {args.out}")


if __name__ == "__main__":
    main()
