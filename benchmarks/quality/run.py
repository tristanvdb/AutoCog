#!/usr/bin/env python3
"""Quality benchmark: task accuracy, token overhead, and constraint friction
per rendering syntax (the v0.7.2 scorer).

The mcq demo family is secretly a benchmark: `select(choices)` has a known
correct answer. For every (syntax, demo, question) this driver runs the demo
through the Engine (capturing per-step input/frame/fta artifacts), scores
correctness, then re-encodes each step's frame with `efta --score` to get the
canonical forced path with real P(token | prefix) on *every* token — value
and structural alike. From that single artifact:

  - tokens.value / tokens.structure — the token overhead a syntax imposes
    (structural = nodes without a schema field: headers, labels, separators);
  - friction.structure / friction.value — mean -log P per forced token: how
    hard the model fights tokens it never chose. The special-token syntax is
    *expected* to show extreme structural friction on a base model; that
    number is the fine-tuning arc's before-picture.

Results: ECS-flavored NDJSON (one event per run) + a markdown summary,
written as results-<host>-<model>.{ndjson,md}.

    run.py --build <release-build-dir> (--model <gguf> | --rng)
           [--syntaxes complete,stripped,...] [--demos select,...]
           [--questions N] [--out DIR]

NEVER benchmark a Debug/coverage build (see benchmarks/compute/run.sh).
"""

import argparse
import datetime
import json
import math
import os
import platform
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

SYNTAXES = ["complete", "indent-index", "indent", "stripped", "chatml", "llama2chat", "special"]
DEMOS = ["select", "select-cot", "select-hyp", "repeat", "repeat-cot", "repeat-hyp"]


def extract_answer(result):
    if isinstance(result, str):
        return result
    if isinstance(result, dict):
        return result.get("answer")
    return None


def walk_tokens(node, depth, acc):
    """Sum tokens/logprobs of an encoded (single-path) FTT, split by value vs
    structural nodes. The root (unconditioned prompt) is excluded."""
    if depth > 0:
        kind = "value" if node.get("field") is not None else "structure"
        acc[f"tokens.{kind}"] += len(node["tokens"])
        acc[f"nll.{kind}"] += sum(node["logprobs"])
    for child in node.get("children", []):
        walk_tokens(child, depth + 1, acc)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True, help="Release build dir (with tools/)")
    ap.add_argument("--model", help="GGUF model path")
    ap.add_argument("--rng", action="store_true", help="RNG model (pipeline smoke; accuracy is noise)")
    ap.add_argument("--syntaxes", default=",".join(SYNTAXES))
    ap.add_argument("--demos", default=",".join(DEMOS))
    ap.add_argument("--questions", type=int, default=0, help="limit (0 = all)")
    ap.add_argument("--questions-file", default=os.path.join(HERE, "questions.json"),
                    help="question set (default: the built-in 100; see convert.py for ARC/MMLU)")
    ap.add_argument("--out", default=HERE)
    ap.add_argument("--ctx", type=int, default=2048)
    args = ap.parse_args()
    if not args.model and not args.rng:
        sys.exit("one of --model or --rng is required")

    import autocog
    from autocog.recorder import Recorder

    tools = {t: os.path.join(args.build, "tools", t, t) for t in ("stlc", "efta")}
    for t, p in tools.items():
        if not os.path.exists(p):
            sys.exit(f"missing tool: {p} (build the Release tree first)")

    questions = json.load(open(args.questions_file))
    if args.questions:
        questions = questions[: args.questions]
    syntaxes = args.syntaxes.split(",")
    demos = args.demos.split(",")
    model_tag = "rng" if args.rng else os.path.splitext(os.path.basename(args.model))[0]
    host = platform.node().split(".")[0]
    search = os.path.join(REPO, "share", "search", "default.json")

    os.makedirs(args.out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="autocog-quality-")

    # One STA per demo (syntax-independent), compiled once for efta.
    stas = {}
    for demo in demos:
        sta = os.path.join(work, f"{demo}.sta")
        r = subprocess.run([tools["stlc"], "--sta", sta,
                            "-I", os.path.join(REPO, "share", "library", "stlib"),
                            "-I", os.path.join(REPO, "share", "demos", "mcq"),
                            os.path.join(REPO, "share", "demos", "mcq", f"{demo}.stl")],
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"stlc failed for {demo}:\n{r.stderr[-1000:]}")
        stas[demo] = sta

    events = []
    base = {
        "log.level": "info",
        "log.logger": "autocog.bench.quality",
        "event.kind": "metric",
        "event.action": "quality.run",
        "service.name": "autocog",
        "host.name": host,
        "autocog.bench.model": model_tag,
    }

    for syntax in syntaxes:
        syntax_path = os.path.join(REPO, "share", "syntax", f"{syntax}.json")
        engine_kwargs = dict(syntax=syntax_path, search=search)
        if not args.rng:
            engine_kwargs.update(model=args.model, n_ctx=args.ctx)
        engine = autocog.Engine(**engine_kwargs)

        for demo in demos:
            prog = autocog.compile(os.path.join(REPO, "share", "demos", "mcq", f"{demo}.stl"))
            for q in questions:
                engine.set_seed(42)
                rec = Recorder(kinds={"input", "frame", "fta"},  # input doubles as efta content
                               path=os.path.join(work, f"rec-{syntax}-{demo}-{q['id']}"))
                t0 = time.time()
                error = None
                try:
                    result = engine.run(prog, recorder=rec, topic=q["topic"],
                                        question=q["question"], choices=q["choices"])
                except Exception as e:  # a syntax/demo pair failing is a datapoint
                    result, error = None, f"{type(e).__name__}: {e}"
                wall = time.time() - t0

                answer = extract_answer(result)
                ev = dict(base)
                ev["@timestamp"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
                ev.update({
                    "autocog.bench.syntax": syntax,
                    "autocog.bench.demo": demo,
                    "autocog.bench.question": q["id"],
                    "autocog.bench.wall_seconds": round(wall, 3),
                    "autocog.bench.answer": answer,
                    "autocog.bench.correct": (answer == q["answer"]) if answer is not None else None,
                })
                if error:
                    ev["error.message"] = error
                    events.append(ev)
                    print(f"[{syntax}/{demo}/{q['id']}] ERROR {error[:80]}", flush=True)
                    continue

                # Scored canonical path per step: batch every recorded
                # (fta, frame) pair through one efta process / model load.
                jobs = []
                for root, _, files in os.walk(rec.path):
                    for f in sorted(files):
                        if not f.endswith(".frame.json"):
                            continue
                        step = f[: -len(".frame.json")]
                        fta = os.path.join(root, f"{step}.fta.json")
                        if not os.path.exists(fta):
                            continue
                        prompt = os.path.basename(root)
                        job = {"sta": stas[demo], "fta": fta, "prompt": prompt,
                               "frame": os.path.join(root, f),
                               "ftt": os.path.join(root, f"{step}.scored.json")}
                        content = os.path.join(root, f"{step}.input.json")
                        if os.path.exists(content):
                            job["content"] = content
                        jobs.append(job)
                acc = {"tokens.value": 0, "tokens.structure": 0, "nll.value": 0.0, "nll.structure": 0.0}
                if jobs:
                    manifest = os.path.join(rec.path, "batch.json")
                    json.dump(jobs, open(manifest, "w"))
                    cmd = [tools["efta"], "--batch", manifest, "--score", "--ctx", str(args.ctx)]
                    cmd += ["--rng"] if args.rng else ["--model", args.model]
                    r = subprocess.run(cmd, capture_output=True, text=True)
                    if r.returncode != 0:
                        ev["error.message"] = f"efta: {r.stderr[-300:]}"
                    else:
                        for job in jobs:
                            walk_tokens(json.load(open(job["ftt"])), 0, acc)
                ev.update({
                    "autocog.bench.steps": len(jobs),
                    "autocog.bench.tokens.value": acc["tokens.value"],
                    "autocog.bench.tokens.structure": acc["tokens.structure"],
                    "autocog.bench.friction.value":
                        round(acc["nll.value"] / acc["tokens.value"], 4) if acc["tokens.value"] else None,
                    "autocog.bench.friction.structure":
                        round(acc["nll.structure"] / acc["tokens.structure"], 4) if acc["tokens.structure"] else None,
                })
                events.append(ev)
                print(f"[{syntax}/{demo}/{q['id']}] correct={ev['autocog.bench.correct']} "
                      f"tok(v/s)={acc['tokens.value']}/{acc['tokens.structure']} "
                      f"friction(v/s)={ev['autocog.bench.friction.value']}/{ev['autocog.bench.friction.structure']} "
                      f"{wall:.1f}s", flush=True)

    stamp = f"{host}-{model_tag}"
    nd = os.path.join(args.out, f"results-{stamp}.ndjson")
    with open(nd, "w") as f:
        for ev in events:
            f.write(json.dumps(ev) + "\n")

    md = os.path.join(args.out, f"results-{stamp}.md")
    with open(md, "w") as f:
        f.write(f"# Quality benchmark — {host} — {model_tag}\n\n")
        f.write("| syntax | demo | accuracy | tok value | tok structure | friction value | friction structure |\n")
        f.write("|---|---|---|---|---|---|---|\n")
        for syntax in syntaxes:
            for demo in demos:
                rows = [e for e in events
                        if e["autocog.bench.syntax"] == syntax and e["autocog.bench.demo"] == demo
                        and "error.message" not in e]
                if not rows:
                    f.write(f"| {syntax} | {demo} | (all failed) | | | | |\n")
                    continue
                scored = [e for e in rows if e["autocog.bench.correct"] is not None]
                acc_pct = (f"{100.0 * sum(e['autocog.bench.correct'] for e in scored) / len(scored):.0f}%"
                           if scored else "n/a")
                def mean(key):
                    vals = [e[key] for e in rows if e.get(key) is not None]
                    return f"{sum(vals) / len(vals):.2f}" if vals else "n/a"
                f.write(f"| {syntax} | {demo} | {acc_pct} | {mean('autocog.bench.tokens.value')} "
                        f"| {mean('autocog.bench.tokens.structure')} | {mean('autocog.bench.friction.value')} "
                        f"| {mean('autocog.bench.friction.structure')} |\n")
    print(f"\nresults: {nd}\n         {md}")


if __name__ == "__main__":
    main()
