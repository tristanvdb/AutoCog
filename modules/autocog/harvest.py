"""Harvest recorder output into re-rendered, token-level traces.

The recorder captures per-step ``input``/``frame`` artifacts; the frame is
syntax-independent. This tool re-renders every recorded step under each
target syntax: recompile the program's prompt under syntax S (``ista``),
encode the recorded frame against that FTA (``efta``), optionally scoring
the forced path against a model. One recorded run therefore becomes a
token-level training trace under *every* syntax — including syntaxes the
generating model has never seen.

Output layout (self-contained; the dataset exporter consumes it alone)::

    {out}/harvest.json                                — versioned manifest
    {out}/program.sta                                 — compiled STA
    {out}/{syntax}/ctx-{id}/{prompt}/{step}.ftt.json  — re-rendered FTTs
    {out}/source/ctx-{id}/{prompt}/{step}.{frame,input}.json — copied inputs

Usage::

    python -m autocog.harvest --records DIR --stl PROG.stl [-I DIR]...
        --syntaxes complete,special --out DIR (--model GGUF | --rng)
        [--score] [--search FILE] [--tools DIR]
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

HARVEST_FORMAT = "autocog-harvest"
HARVEST_VERSION = 1


def _tool(name, tools_dir=None):
    """Resolve a pipeline tool: an explicit build tree or the installed PATH."""
    if tools_dir:
        return os.path.join(tools_dir, "tools", name, name)
    return name


def _run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"FAILED ({r.returncode}): {' '.join(cmd)}\n{r.stderr[-2000:]}")
    return r


def collect_steps(records):
    """Yield (ctx, prompt, step, frame_path, input_path) for every recorded
    step that has a frame (input may be absent for input-less prompts)."""
    for entry in sorted(os.listdir(records)):
        ctx_dir = os.path.join(records, entry)
        if not entry.startswith("ctx-") or not os.path.isdir(ctx_dir):
            continue
        ctx = entry[len("ctx-"):]
        for prompt in sorted(os.listdir(ctx_dir)):
            step_dir = os.path.join(ctx_dir, prompt)
            if not os.path.isdir(step_dir):
                continue
            for fname in sorted(os.listdir(step_dir)):
                if not fname.endswith(".frame.json"):
                    continue
                step = fname[: -len(".frame.json")]
                inp = os.path.join(step_dir, f"{step}.input.json")
                yield (ctx, prompt, step, os.path.join(step_dir, fname),
                       inp if os.path.exists(inp) else None)


def harvest(records, stl, out, syntaxes, includes=(), search=None,
            model=None, rng=False, score=False, tools_dir=None, ctx_size=2048,
            share=None):
    """Re-render every recorded step under each target syntax. Returns the
    manifest (also written to {out}/harvest.json)."""
    if not model and not rng:
        raise ValueError("one of model= or rng=True is required")
    share = share or os.environ.get("AUTOCOG_SHARE")

    def resolve_config(kind, name):
        if os.path.exists(name):
            return name
        if share:
            candidate = os.path.join(share, kind, f"{name}.json")
            if os.path.exists(candidate):
                return candidate
        raise ValueError(f"cannot resolve {kind} '{name}' (pass a path or --share)")

    os.makedirs(out, exist_ok=True)
    sta = os.path.join(out, "program.sta")
    cmd = [_tool("stlc", tools_dir), "--sta", sta]
    for inc in includes:
        cmd += ["-I", inc]
    cmd.append(stl)
    _run(cmd)

    steps = list(collect_steps(records))
    if not steps:
        raise ValueError(f"no recorded steps with frames under {records}")

    # Copy source artifacts so the harvest is self-contained.
    for ctx, prompt, step, frame, inp in steps:
        src_dir = os.path.join(out, "source", f"ctx-{ctx}", prompt)
        os.makedirs(src_dir, exist_ok=True)
        shutil.copy(frame, os.path.join(src_dir, f"{step}.frame.json"))
        if inp:
            shutil.copy(inp, os.path.join(src_dir, f"{step}.input.json"))

    manifest_steps = []
    search_path = resolve_config("search", search or "default")
    for syntax in syntaxes:
        syntax_path = resolve_config("syntax", syntax)

        jobs = []
        for ctx, prompt, step, frame, inp in steps:
            dst_dir = os.path.join(out, syntax, f"ctx-{ctx}", prompt)
            os.makedirs(dst_dir, exist_ok=True)
            fta = os.path.join(dst_dir, f"{step}.fta.json")
            cmd = [_tool("ista", tools_dir), "--sta", sta, "--prompt", prompt,
                   "--syntax", syntax_path, "--search", search_path,
                   "--content", inp if inp else "{}", "--fta", fta]
            _run(cmd)
            job = {"sta": sta, "fta": fta, "prompt": prompt, "frame": frame,
                   "ftt": os.path.join(dst_dir, f"{step}.ftt.json")}
            if inp:
                job["content"] = inp
            jobs.append(job)
            manifest_steps.append({
                "ctx": ctx, "prompt": prompt, "step": step, "syntax": syntax,
                "ftt": os.path.relpath(job["ftt"], out),
                "frame": os.path.join("source", f"ctx-{ctx}", prompt, f"{step}.frame.json"),
            })

        manifest_path = os.path.join(out, f"batch-{os.path.basename(syntax)}.json")
        json.dump(jobs, open(manifest_path, "w"))
        cmd = [_tool("efta", tools_dir), "--batch", manifest_path, "--ctx", str(ctx_size)]
        if score:
            cmd.append("--score")
        cmd += ["--rng"] if rng else ["--model", model]
        _run(cmd)
        os.unlink(manifest_path)

    manifest = {
        "format": HARVEST_FORMAT,
        "version": HARVEST_VERSION,
        "records": os.path.abspath(records),
        "stl": os.path.abspath(stl),
        "sta": "program.sta",
        "syntaxes": list(syntaxes),
        "model": "rng" if rng else os.path.splitext(os.path.basename(model))[0],
        "scored": bool(score),
        "steps": manifest_steps,
    }
    with open(os.path.join(out, "harvest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    return manifest


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m autocog.harvest",
                                 description=__doc__.split("\n\n")[0])
    ap.add_argument("--records", required=True, help="recorder output directory")
    ap.add_argument("--stl", required=True, help="the recorded program's STL source")
    ap.add_argument("-I", "--include", action="append", default=[],
                    help="STL include path (repeatable)")
    ap.add_argument("--syntaxes", required=True,
                    help="comma-separated syntax names (share/syntax/) or file paths")
    ap.add_argument("--out", required=True, help="harvest output directory")
    ap.add_argument("--model", help="GGUF model (tokenizer, and scorer with --score)")
    ap.add_argument("--rng", action="store_true", help="RNG model (byte-level tokenizer)")
    ap.add_argument("--score", action="store_true",
                    help="score forced paths: logprobs = P(token | prefix)")
    ap.add_argument("--search", help="search config (default: share/search/default.json)")
    ap.add_argument("--tools", help="build tree with tools/ (default: installed PATH)")
    ap.add_argument("--share", help="share/ directory for resolving syntax/search names "
                                    "(default: $AUTOCOG_SHARE)")
    ap.add_argument("--ctx", type=int, default=2048)
    args = ap.parse_args(argv)

    manifest = harvest(args.records, args.stl, args.out, args.syntaxes.split(","),
                       includes=args.include, search=args.search, model=args.model,
                       rng=args.rng, score=args.score, tools_dir=args.tools,
                       ctx_size=args.ctx, share=args.share)
    n = len(manifest["steps"])
    print(f"harvested {n} step renderings ({len(manifest['syntaxes'])} syntaxes) -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
