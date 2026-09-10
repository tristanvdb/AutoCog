#!/usr/bin/env python3
"""Computational performance sweep over search parameters.

Compiles a workload once per STL variant, instantiates one FTA per cell,
evaluates each with xfta --perf, and appends the ECS NDJSON eval.summary
event (cell parameters attached under autocog.bench.*) to the results file
*as each cell finishes* — an interrupted or budget-stopped sweep keeps
everything it measured. A markdown table is (re)written at the end.

Two ways to define the cells:

  default / --quick     the classic beams x ahead x width matrix
  --cells FILE.json     an explicit list of cells; each cell may set
                        beams, ahead, width, topk, threshold, repetition,
                        metric (queue ordering list), slots (KV slot pool,
                        via AUTOCOG_KV_SLOTS), stl (workload variant path),
                        syntax (path), ctx, label

--budget-seconds N stops launching new cells once N seconds elapsed
(skipped cells are logged) — for unattended runs.

Driven by run.sh; can also be invoked directly:

    python3 sweep.py --build <release-build-dir> (--model <gguf> | --rng)
                     [--cells cells.json | --quick] [--budget-seconds N]
                     [--out <results-dir>] [--tag <suffix>]
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

# The classic matrix: each axis stresses a different part of the machinery.
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

CELL_KEYS = ("label", "beams", "ahead", "width", "topk", "threshold",
             "repetition", "metric", "slots", "stl", "syntax", "ctx")


def search_config(cell):
    return {
        "text": {
            "threshold": cell.get("threshold", 0.1),
            "beams": cell.get("beams", 4),
            "topk": cell.get("topk"),
            "ahead": cell.get("ahead", 1),
            "width": cell.get("width", 1),
            "repetition": cell.get("repetition"),
            "diversity": None,
        },
        "enum":   {"threshold": 0.1, "width": 1},
        "branch": {"threshold": 0.1, "width": 1},
        "flow":   {"threshold": 0.1, "width": 1},
        "queue":  {"metric": cell.get("metric", ["perplexity"])},
    }


def run(cmd, env=None):
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        sys.exit(f"FAILED ({r.returncode}): {' '.join(cmd)}\n{r.stderr[-2000:]}")
    return r


def cell_name(cell):
    if cell.get("label"):
        return cell["label"]
    parts = [f"b{cell.get('beams', 4)}", f"a{cell.get('ahead', 1)}", f"w{cell.get('width', 1)}"]
    for key in ("topk", "threshold", "slots"):
        if cell.get(key) is not None:
            parts.append(f"{key[0]}{cell[key]}")
    return " ".join(parts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True, help="Release build dir (with tools/)")
    ap.add_argument("--model", help="GGUF model path")
    ap.add_argument("--rng", action="store_true", help="RNG model (harness-overhead floor)")
    ap.add_argument("--out", default=HERE, help="results directory")
    ap.add_argument("--quick", action="store_true", help="reduced matrix")
    ap.add_argument("--cells", help="explicit cell list (JSON)")
    ap.add_argument("--budget-seconds", type=float, default=0,
                    help="stop launching new cells past this (0 = no budget)")
    ap.add_argument("--tag", default="", help="suffix for the results file name")
    ap.add_argument("--ctx", type=int, default=2048)
    args = ap.parse_args()
    if not args.model and not args.rng:
        sys.exit("one of --model or --rng is required")

    tools = {t: os.path.join(args.build, "tools", t, t) for t in ("stlc", "ista", "xfta")}
    for t, p in tools.items():
        if not os.path.exists(p):
            sys.exit(f"missing tool: {p} (build the Release tree first — see run.sh)")

    if args.cells:
        cells = json.load(open(args.cells))
    else:
        matrix = QUICK_MATRIX if args.quick else FULL_MATRIX
        cells = [dict(zip(matrix.keys(), combo))
                 for combo in itertools.product(*matrix.values())]

    model_tag = "rng" if args.rng else os.path.splitext(os.path.basename(args.model))[0]
    host = platform.node().split(".")[0]
    stamp = f"{host}-{model_tag}" + (f"-{args.tag}" if args.tag else "")
    os.makedirs(args.out, exist_ok=True)
    nd_path = os.path.join(args.out, f"results-{stamp}.ndjson")

    work = tempfile.mkdtemp(prefix="autocog-bench-")
    stas = {}  # stl path -> compiled sta

    def sta_for(stl):
        if stl not in stas:
            sta = os.path.join(work, f"bench-{len(stas)}.sta")
            run([tools["stlc"], "--sta", sta, stl])
            stas[stl] = sta
        return stas[stl]

    results = []
    t_start = time.time()
    nd = open(nd_path, "a")
    for i, cell in enumerate(cells):
        if args.budget_seconds and (time.time() - t_start) > args.budget_seconds:
            print(f"[budget] {args.budget_seconds:.0f}s exhausted — skipping "
                  f"{len(cells) - i} remaining cell(s): "
                  + ", ".join(cell_name(c) for c in cells[i:]), flush=True)
            break
        stl = cell.get("stl") or os.path.join(HERE, "benchmark.stl")
        if not os.path.isabs(stl):
            stl = os.path.join(REPO, stl)
        syntax = cell.get("syntax") or os.path.join(REPO, "share", "syntax", "default.json")
        if not os.path.isabs(syntax):
            syntax = os.path.join(REPO, syntax)
        ctx = cell.get("ctx", args.ctx)

        scfg = os.path.join(work, "search.json")
        with open(scfg, "w") as f:
            json.dump(search_config(cell), f)
        fta = os.path.join(work, "bench.fta")
        run([tools["ista"], "--sta", sta_for(stl), "--prompt", "main", "--syntax", syntax,
             "--search", scfg, "--content", CONTENT, "--fta", fta])

        env = dict(os.environ)
        if cell.get("slots"):
            env["AUTOCOG_KV_SLOTS"] = str(cell["slots"])
        perf = os.path.join(work, "perf.ndjson")
        t0 = time.time()
        cmd = [tools["xfta"], "--fta", fta, "--ftt", os.path.join(work, "out.ftt"),
               "--perf", perf, "--ctx", str(ctx), "--seed", "42"]
        cmd += ["--rng"] if args.rng else ["--model", args.model]
        run(cmd, env=env)
        wall = time.time() - t0

        events = [json.loads(l) for l in open(perf)]
        summary = next(e for e in events if e["event.action"] == "eval.summary")
        for k in CELL_KEYS:
            if cell.get(k) is not None:
                summary[f"autocog.bench.{k}"] = cell[k]
        for k, v in (("beams", 4), ("ahead", 1), ("width", 1)):
            summary.setdefault(f"autocog.bench.{k}", v)
        summary["autocog.bench.wall_seconds"] = round(wall, 3)
        summary["autocog.bench.model"] = model_tag
        results.append(summary)
        nd.write(json.dumps(summary) + "\n")
        nd.flush()
        print(f"[{i + 1}/{len(cells)}] {cell_name(cell)} -> "
              f"{summary['autocog.perf.advance_seconds']:.2f}s eval, "
              f"restore={summary['autocog.perf.tokens.restore']} "
              f"eval={summary['autocog.perf.tokens.eval']} "
              f"decode_calls={summary.get('autocog.perf.decode.calls', '-')}", flush=True)
    nd.close()

    if not results:
        sys.exit("no cells completed")
    md = os.path.join(args.out, f"results-{stamp}.md")
    cols = ["cell", "eval s", "wall s", "tok restore", "tok eval", "decode calls",
            "decode s", "sample s", "kv forks", "complete s", "choose s"]
    with open(md, "w") as f:
        f.write(f"# Compute benchmark — {host} — {model_tag}"
                + (f" — {args.tag}" if args.tag else "") + "\n\n")
        f.write(f"CPU: {results[0].get('autocog.perf.host.cpu', '?')}  \n")
        f.write(f"Build: {results[0].get('autocog.perf.build_type', '?')}  \n")
        f.write(f"Version: {results[0].get('service.version', '?')}  \n")
        f.write(f"NGL: {os.environ.get('AUTOCOG_NGL', '0')}\n\n")
        f.write("| " + " | ".join(cols) + " |\n")
        f.write("|" + "---|" * len(cols) + "\n")
        for r in results:
            name = r.get("autocog.bench.label") or (
                f"b{r['autocog.bench.beams']} a{r['autocog.bench.ahead']} w{r['autocog.bench.width']}")
            row = [name,
                   f"{r['autocog.perf.advance_seconds']:.2f}",
                   f"{r['autocog.bench.wall_seconds']:.2f}",
                   str(r["autocog.perf.tokens.restore"]),
                   str(r["autocog.perf.tokens.eval"]),
                   str(r.get("autocog.perf.decode.calls", "-")),
                   f"{r.get('autocog.perf.decode.seconds', 0):.2f}",
                   f"{r.get('autocog.perf.sample.seconds', 0):.2f}",
                   str(r.get("autocog.perf.kv.forks", "-")),
                   f"{r['autocog.perf.complete.seconds']:.2f}",
                   f"{r['autocog.perf.choose.seconds']:.2f}"]
            f.write("| " + " | ".join(row) + " |\n")
    print(f"\nresults: {nd_path}\n         {md}")


if __name__ == "__main__":
    main()
