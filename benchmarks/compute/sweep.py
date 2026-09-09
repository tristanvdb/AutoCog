#!/usr/bin/env python3
"""Computational performance sweep over search parameters.

Compiles benchmark.stl once, instantiates one FTA per search configuration,
evaluates each with xfta --perf, and aggregates the ECS NDJSON perf events
into results files: a machine-readable NDJSON stream (one eval.summary event
per configuration, sweep parameters attached under autocog.bench.*) and a
human-readable markdown table.

Driven by run.sh; can also be invoked directly:

    python3 sweep.py --build <release-build-dir> (--model <gguf> | --rng) \
                     [--out <results-dir>] [--quick]
"""

import argparse
import itertools
import json
import os
import platform
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

CONTENT = json.dumps({
    "topic": "Science",
    "question": "What is H2O?",
    "choices": ["Water", "Fire", "Air", "Earth"],
})

# The sweep matrix: each axis stresses a different part of the machinery.
#   beams — branch fan-out (branch ping-pong cost)
#   ahead — lookahead rollouts (branch/rewind cost, linear multiplier)
#   width — surviving results (FTT growth)
FULL_MATRIX = {
    "beams": [1, 2, 4, 8],
    "ahead": [1, 2, 4],
    "width": [1, 2],
}
QUICK_MATRIX = {
    "beams": [1, 4],
    "ahead": [1, 2],
    "width": [1],
}


def search_config(beams, ahead, width):
    return {
        "text":   {"threshold": 0.1, "beams": beams, "ahead": ahead, "width": width,
                   "repetition": None, "diversity": None},
        "enum":   {"threshold": 0.1, "width": 1},
        "branch": {"threshold": 0.1, "width": 1},
        "flow":   {"threshold": 0.1, "width": 1},
        "queue":  {"metric": "perplexity"},
    }


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        sys.exit(f"FAILED ({r.returncode}): {' '.join(cmd)}\n{r.stderr[-2000:]}")
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True, help="Release build dir (with tools/)")
    ap.add_argument("--model", help="GGUF model path")
    ap.add_argument("--rng", action="store_true", help="RNG model (harness-overhead floor)")
    ap.add_argument("--out", default=HERE, help="results directory")
    ap.add_argument("--quick", action="store_true", help="reduced matrix")
    ap.add_argument("--ctx", type=int, default=2048)
    args = ap.parse_args()
    if not args.model and not args.rng:
        sys.exit("one of --model or --rng is required")

    tools = {t: os.path.join(args.build, "tools", t, t) for t in ("stlc", "ista", "xfta")}
    for t, p in tools.items():
        if not os.path.exists(p):
            sys.exit(f"missing tool: {p} (build the Release tree first — see run.sh)")

    syntax = os.path.join(REPO, "share", "syntax", "default.json")
    matrix = QUICK_MATRIX if args.quick else FULL_MATRIX
    model_tag = "rng" if args.rng else os.path.splitext(os.path.basename(args.model))[0]
    host = platform.node().split(".")[0]

    work = tempfile.mkdtemp(prefix="autocog-bench-")
    sta = os.path.join(work, "bench.sta")
    run([tools["stlc"], "--sta", sta, os.path.join(HERE, "benchmark.stl")])

    results = []
    combos = list(itertools.product(*matrix.values()))
    for i, combo in enumerate(combos):
        params = dict(zip(matrix.keys(), combo))
        scfg = os.path.join(work, "search.json")
        with open(scfg, "w") as f:
            json.dump(search_config(**params), f)
        fta = os.path.join(work, "bench.fta")
        run([tools["ista"], "--sta", sta, "--prompt", "main", "--syntax", syntax,
             "--search", scfg, "--content", CONTENT, "--fta", fta])

        perf = os.path.join(work, "perf.ndjson")
        t0 = time.time()
        cmd = [tools["xfta"], "--fta", fta, "--ftt", os.path.join(work, "out.ftt"),
               "--perf", perf, "--ctx", str(args.ctx), "--seed", "42"]
        cmd += ["--rng"] if args.rng else ["--model", args.model]
        run(cmd)
        wall = time.time() - t0

        events = [json.loads(l) for l in open(perf)]
        summary = next(e for e in events if e["event.action"] == "eval.summary")
        for k, v in params.items():
            summary[f"autocog.bench.{k}"] = v
        summary["autocog.bench.wall_seconds"] = round(wall, 3)
        summary["autocog.bench.model"] = model_tag
        results.append(summary)
        print(f"[{i + 1}/{len(combos)}] {params} -> "
              f"{summary['autocog.perf.advance_seconds']:.2f}s eval, "
              f"restore={summary['autocog.perf.tokens.restore']} "
              f"eval={summary['autocog.perf.tokens.eval']}", flush=True)

    stamp = f"{host}-{model_tag}"
    nd = os.path.join(args.out, f"results-{stamp}.ndjson")
    with open(nd, "w") as f:
        for r in results:
            f.write(json.dumps(r) + "\n")

    md = os.path.join(args.out, f"results-{stamp}.md")
    with open(md, "w") as f:
        f.write(f"# Compute benchmark — {host} — {model_tag}\n\n")
        f.write(f"CPU: {results[0].get('autocog.perf.host.cpu', '?')}  \n")
        f.write(f"Build: {results[0].get('autocog.perf.build_type', '?')}  \n")
        f.write(f"Version: {results[0].get('service.version', '?')}\n\n")
        cols = list(matrix.keys()) + ["eval s", "wall s", "tok restore", "tok eval",
                                      "tok lookahead", "complete s", "choose s", "text s"]
        f.write("| " + " | ".join(cols) + " |\n")
        f.write("|" + "---|" * len(cols) + "\n")
        for r in results:
            row = [str(r[f"autocog.bench.{k}"]) for k in matrix.keys()]
            row += [f"{r['autocog.perf.advance_seconds']:.2f}",
                    f"{r['autocog.bench.wall_seconds']:.2f}",
                    str(r["autocog.perf.tokens.restore"]),
                    str(r["autocog.perf.tokens.eval"]),
                    str(r["autocog.perf.complete.tokens.lookahead"]),
                    f"{r['autocog.perf.complete.seconds']:.2f}",
                    f"{r['autocog.perf.choose.seconds']:.2f}",
                    f"{r['autocog.perf.text.seconds']:.2f}"]
            f.write("| " + " | ".join(row) + " |\n")
    print(f"\nresults: {nd}\n         {md}")


if __name__ == "__main__":
    main()
