"""bench quality — MCQ accuracy / token overhead / constraint friction.

The benchmarks/quality/run.py semantics through the in-process worker:
one model load per model (not per syntax), scoring via Engine.score_frame
(no efta subprocess), datapoints via --data + --formatter.
"""

import json
import os
import tempfile
import time

import autocog
from autocog.recorder import Recorder

from . import results
from .formatters import load_formatter, load_questions
from .perf import find_root
from .workers import LocalWorker, pick_worker

SYNTAXES = ["complete", "indent-index", "indent", "stripped",
            "chatml", "llama2chat", "llama3chat", "special"]
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


def run_quality(model=None, data=None, formatter="", questions=0,
                syntaxes=None, demos=None, out=".", root=None, seed=42,
                n_ctx=2048, workers=None, log=print):
    """Run the quality matrix; returns the list of events."""
    root = root or find_root(os.getcwd())
    if root is None:
        raise autocog.errors.ConfigError("cannot locate the repo root (share/syntax)")
    data = data or os.path.join(root, "benchmarks", "quality", "questions.json")
    qs = load_questions(data, load_formatter(formatter), limit=questions)
    syntaxes = syntaxes or ["complete", "stripped"]
    demos = demos or ["select"]

    mcq_dir = os.path.join(root, "share", "demos", "mcq")
    search = os.path.join(root, "share", "search", "default.json")
    model_tag = os.path.splitext(os.path.basename(model))[0] if model else "rng"
    os.makedirs(out, exist_ok=True)
    stamp = f"{results.host_name()}-{model_tag}"
    nd = results.NdjsonWriter(os.path.join(out, f"results-{stamp}.ndjson"))

    if workers:
        worker = pick_worker(workers, model)
        log(f"worker: {worker.url} hosts {worker.capabilities()['models']}")
    else:
        worker = LocalWorker(model=model, n_ctx=n_ctx)
    programs = {demo: autocog.compile(os.path.join(mcq_dir, f"{demo}.stl"),
                                      includes=[mcq_dir])
                for demo in demos}

    base = {
        "log.logger": "autocog.bench.quality",
        "event.action": "quality.run",
        "autocog.bench.model": model_tag,
    }

    for syntax in syntaxes:
        engine = worker.engine(os.path.join(root, "share", "syntax", f"{syntax}.json"),
                               search)
        for demo in demos:
            prog = programs[demo]
            for q in qs:
                worker.set_seed(seed)
                with tempfile.TemporaryDirectory() as tmp:
                    rec = Recorder(kinds={"input", "frame"}, path=tmp)
                    t0 = time.time()
                    error = None
                    try:
                        result = engine.run(prog, recorder=rec, topic=q.get("topic", ""),
                                            question=q["question"], choices=q["choices"])
                    except Exception as e:  # a failing pair is a datapoint
                        result, error = None, f"{type(e).__name__}: {e}"
                    wall = time.time() - t0

                    answer = extract_answer(result)
                    ev = results.base_event("autocog.bench.quality", "quality.run", dict(base))
                    ev.update({
                        "autocog.bench.syntax": syntax,
                        "autocog.bench.demo": demo,
                        "autocog.bench.question": q["id"],
                        "autocog.bench.n_choices": len(q["choices"]),
                        "autocog.bench.wall_seconds": round(wall, 3),
                        "autocog.bench.answer": answer,
                        "autocog.bench.correct":
                            (answer == q["answer"]) if answer is not None else None,
                    })
                    if error:
                        ev["error.message"] = error
                        nd.emit(ev)
                        log(f"[{syntax}/{demo}/{q['id']}] ERROR {error[:80]}")
                        continue

                    # Score the canonical forced path of every recorded step
                    # in-process (the efta --score semantics).
                    acc = {"tokens.value": 0, "tokens.structure": 0,
                           "nll.value": 0.0, "nll.structure": 0.0}
                    steps = 0
                    for walk_dir, _, files in os.walk(tmp):
                        for f in sorted(files):
                            if not f.endswith(".frame.json"):
                                continue
                            step = f[: -len(".frame.json")]
                            frame = json.load(open(os.path.join(walk_dir, f)))
                            input_file = os.path.join(walk_dir, f"{step}.input.json")
                            content = (json.load(open(input_file))
                                       if os.path.exists(input_file) else {})
                            prompt = os.path.basename(walk_dir)
                            scored = engine.score_frame(prog, prompt, frame, content)
                            walk_tokens(scored, 0, acc)
                            steps += 1

                ev.update({
                    "autocog.bench.steps": steps,
                    "autocog.bench.tokens.value": acc["tokens.value"],
                    "autocog.bench.tokens.structure": acc["tokens.structure"],
                    "autocog.bench.friction.value":
                        round(acc["nll.value"] / acc["tokens.value"], 4)
                        if acc["tokens.value"] else None,
                    "autocog.bench.friction.structure":
                        round(acc["nll.structure"] / acc["tokens.structure"], 4)
                        if acc["tokens.structure"] else None,
                })
                nd.emit(ev)
                log(f"[{syntax}/{demo}/{q['id']}] correct={ev['autocog.bench.correct']} "
                    f"tok(v/s)={acc['tokens.value']}/{acc['tokens.structure']} "
                    f"friction(v/s)={ev['autocog.bench.friction.value']}/"
                    f"{ev['autocog.bench.friction.structure']} {wall:.1f}s")

    nd.close()
    md = os.path.join(out, f"results-{stamp}.md")
    results.quality_md(md, f"{results.host_name()} — {model_tag}", nd.events)
    log(f"\nresults: {nd.path}\n         {md}")
    return nd.events
