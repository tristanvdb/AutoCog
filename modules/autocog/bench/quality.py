"""bench quality — MCQ accuracy / token overhead / constraint friction.

The share/benchmarks/quality/run.py semantics: one model load per model
(not per syntax), scoring via score_frame (no efta subprocess),
datapoints via --data + --formatter.

With --worker, samples run through EnginePools (one per syntax, sharing
a per-tag LaneLedger): the run_samples scheduler keeps every worker lane
primed with sample executions, whose prompt chains and mapped calls
dispatch per-FTA to free lanes. Resume is per SAMPLE: existing result
lines are indexed by (syntax, demo, question) and only the gaps run —
a partial file is data, not a completion marker. Results order follows
completion, not the matrix; consumers group by keys.

Seeding: real-model evaluation is deterministic (the only RNG consumer
is the rng pseudo-model), so worker runs seed once per run and rng runs
are not bit-reproducible under pooled dispatch (plumbing instrument).
"""

import asyncio
import glob
import json
import os
import tempfile
import time

import autocog
from autocog.recorder import Recorder

from . import results
from .formatters import load_formatter, load_questions
from .perf import find_root
from .workers import LocalWorker, model_tag

SYNTAXES = ["complete", "indent-index", "indent", "stripped",
            "chatml", "llama2chat", "llama3chat", "special"]
DEMOS = ["select", "select-cot", "select-hyp", "repeat", "repeat-cot",
         "repeat-hyp", "label"]

#: samples kept in flight per worker lane by the scheduler (keeps the
#: lane's queue primed without materializing whole runs)
WINDOW_PER_LANE = 2

#: choice labels for the `label` mechanism (matches stlib's `letter` vocab)
LABELS = "ABCDEFGH"


def is_label_demo(demo):
    return demo.split("-")[0] == "label"


def already_labelled(choices):
    """Do the choice texts already carry their letter? The ARC converter
    prepends them ("C: mutualism") because ARC answers cross-reference
    options by label."""
    return all(len(c) > 2 and c[0].upper() == LABELS[i] and c[1] in ".:)"
               for i, c in enumerate(choices) if i < len(LABELS))


def choices_for(demo, choices):
    """Content choices for a mechanism: `label` needs the letter visible in
    the document, so it labels them unless the dataset already did (never
    double-labels). Other mechanisms pass the texts through."""
    if is_label_demo(demo) and not already_labelled(choices):
        return [f"{LABELS[i]}. {c}" for i, c in enumerate(choices)]
    return list(choices)


def extract_answer(result):
    if isinstance(result, str):
        return result
    if isinstance(result, dict):
        return result.get("answer")
    return None


def score_answer(demo, raw, q):
    """(recorded answer, correct) for a mechanism's raw output.

    select/repeat yield a choice text directly. `label` yields a letter:
    in range it maps to the choice text; OUT of range (the letter vocab
    spans A-H while an item may offer fewer choices) it is recorded as
    the bare letter and scored WRONG --- an unusable label is a miss, not
    a missing datapoint. Only a genuinely absent answer scores None."""
    if raw is None:
        return None, None
    if not is_label_demo(demo):
        return raw, raw == q["answer"]
    idx = LABELS.find(str(raw).strip().upper()[:1]) if isinstance(raw, str) else -1
    if 0 <= idx < len(q["choices"]):
        text = q["choices"][idx]
        return text, text == q["answer"]
    return raw, False


def walk_tokens(node, depth, acc):
    """Sum tokens/logprobs of an encoded (single-path) FTT, split by value vs
    structural nodes. The root (unconditioned prompt) is excluded."""
    if depth > 0:
        kind = "value" if node.get("field") is not None else "structure"
        acc[f"tokens.{kind}"] += len(node["tokens"])
        acc[f"nll.{kind}"] += sum(node["logprobs"])
    for child in node.get("children", []):
        walk_tokens(child, depth + 1, acc)


def existing_samples(out_dir):
    """Index (syntax, demo, question) triples already recorded under an
    output directory — any results file, any host, torn lines tolerated."""
    have = set()
    for path in glob.glob(os.path.join(out_dir, "results-*.ndjson")):
        for line in open(path):
            try:
                ev = json.loads(line)
            except json.JSONDecodeError:
                continue
            key = (ev.get("autocog.bench.syntax"), ev.get("autocog.bench.demo"),
                   ev.get("autocog.bench.question"))
            if all(k is not None for k in key):
                have.add(key)
    return have


class QualityRun:
    """One quality run as sample state: the ordered sample list, the
    have-index from existing results, lazily opened writer, compiled
    programs. Drives no scheduling itself — run_samples does."""

    def __init__(self, tag, data, out, root, formatter="", questions=0,
                 syntaxes=None, demos=None, log=print):
        self.tag = tag
        self.root = root
        self.out = out
        self.log = log
        self.syntaxes = syntaxes or ["complete", "stripped"]
        self.demos = demos or ["select"]
        self.qs = load_questions(data, load_formatter(formatter),
                                 limit=questions)
        mcq_dir = os.path.join(root, "share", "demos", "mcq")
        self.programs = {demo: autocog.compile(
            os.path.join(mcq_dir, f"{demo}.stl"), includes=[mcq_dir])
            for demo in self.demos}
        self.search = os.path.join(root, "share", "search", "default.json")
        self.samples = [(syntax, demo, q)
                        for syntax in self.syntaxes
                        for demo in self.demos
                        for q in self.qs]
        have = existing_samples(out)
        self.pending = [s for s in self.samples
                        if (s[0], s[1], s[2]["id"]) not in have]
        self.base = {
            "log.logger": "autocog.bench.quality",
            "event.action": "quality.run",
            "autocog.bench.model": tag,
        }
        self._writer = None

    def syntax_path(self, syntax):
        return os.path.join(self.root, "share", "syntax", f"{syntax}.json")

    def make_pools(self, urls, ledger):
        """One EnginePool per syntax, all sharing the tag's lane ledger."""
        from autocog.remote import EnginePool

        return {syntax: EnginePool(urls, model_tag=self.tag,
                                   syntax=self.syntax_path(syntax),
                                   search=self.search, ledger=ledger)
                for syntax in self.syntaxes}

    async def eval_sample(self, pools, syntax, demo, q):
        """Execute one sample (chain + score walk) through the pools;
        every failure is an error datapoint, never an exception."""
        pool = pools[syntax]
        prog = self.programs[demo]
        ev = results.base_event("autocog.bench.quality", "quality.run",
                                dict(self.base))
        ev.update({
            "autocog.bench.syntax": syntax,
            "autocog.bench.demo": demo,
            "autocog.bench.question": q["id"],
            "autocog.bench.n_choices": len(q["choices"]),
        })
        t0 = time.time()
        try:
            with tempfile.TemporaryDirectory() as tmp:
                rec = Recorder(kinds={"input", "frame"}, path=tmp)
                result = await pool.run_async(
                    prog, recorder=rec, topic=q.get("topic", ""),
                    question=q["question"], choices=choices_for(demo, q["choices"]))
                wall = time.time() - t0
                answer, correct = score_answer(demo, extract_answer(result), q)

                # Score the canonical forced path of every recorded step.
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
                        scored = await pool.score_frame_async(
                            prog, prompt, frame, content)
                        walk_tokens(scored, 0, acc)
                        steps += 1
        except Exception as e:  # noqa: BLE001 — a failing sample is a datapoint
            ev.update({
                "autocog.bench.wall_seconds": round(time.time() - t0, 3),
                "autocog.bench.answer": None,
                "autocog.bench.correct": None,
                "error.message": f"{type(e).__name__}: {e}",
            })
            self.log(f"[{syntax}/{demo}/{q['id']}] ERROR "
                     f"{ev['error.message'][:80]}")
            return ev

        ev.update({
            "autocog.bench.wall_seconds": round(wall, 3),
            "autocog.bench.answer": answer,
            "autocog.bench.correct": correct,
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
        self.log(f"[{syntax}/{demo}/{q['id']}] correct={ev['autocog.bench.correct']} "
                 f"tok(v/s)={acc['tokens.value']}/{acc['tokens.structure']} "
                 f"friction(v/s)={ev['autocog.bench.friction.value']}/"
                 f"{ev['autocog.bench.friction.structure']} {wall:.1f}s")
        return ev

    def emit(self, ev):
        if self._writer is None:
            stamp = f"{results.host_name()}-{self.tag}"
            self._writer = results.NdjsonWriter(
                os.path.join(self.out, f"results-{stamp}.ndjson"),
                atomic=False)
        self._writer.emit(ev)

    def finish(self):
        """Close the writer and regenerate the markdown summary from ALL
        result lines (resumed runs span files/appends)."""
        if self._writer is not None:
            self._writer.close()
        events = []
        for path in glob.glob(os.path.join(self.out, "results-*.ndjson")):
            for line in open(path):
                try:
                    ev = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if all(ev.get(f"autocog.bench.{k}") is not None
                       for k in ("syntax", "demo", "question")):
                    events.append(ev)
        if events:
            md = os.path.join(
                self.out, f"results-{results.host_name()}-{self.tag}.md")
            results.quality_md(md, f"{results.host_name()} — {self.tag}", events)
            self.log(f"results: {self.out}")


async def run_samples(entries, ledgers, log=print, window=WINDOW_PER_LANE,
                      on_start=None, on_end=None):
    """The sample scheduler: whenever the window has space, scan the
    entries in order for samples that have no data, whose tag group has
    capacity, and are not in flight — then await completions one-for-one.

    entries: ordered [(QualityRun, pools)]; ledgers: {tag: LaneLedger}.
    on_start/on_end(index): run lifecycle hooks (first sample scheduled /
    last sample landed)."""
    pending = {i: list(qr.pending) for i, (qr, _) in enumerate(entries)}
    remaining = {i: len(p) for i, p in pending.items()}
    inflight = {}                       # task -> entry index
    group = {tag: 0 for tag in ledgers}
    started = set()

    def capacity(tag):
        return window * ledgers[tag].total_lanes() - group[tag]

    while True:
        for i, (qr, pools) in enumerate(entries):
            while pending[i] and capacity(qr.tag) > 0:
                syntax, demo, q = pending[i].pop(0)
                if i not in started:
                    started.add(i)
                    if on_start:
                        on_start(i)
                task = asyncio.ensure_future(
                    qr.eval_sample(pools, syntax, demo, q))
                inflight[task] = i
                group[qr.tag] += 1
        if not inflight:
            break
        done, _ = await asyncio.wait(set(inflight),
                                     return_when=asyncio.FIRST_COMPLETED)
        for task in done:
            i = inflight.pop(task)
            qr = entries[i][0]
            group[qr.tag] -= 1
            qr.emit(task.result())      # eval_sample never raises
            remaining[i] -= 1
            if remaining[i] == 0:
                qr.finish()
                if on_end:
                    on_end(i)


def compatible_urls(workers, tag):
    """The worker URLs hosting a tag (capability-probed, fail-fast)."""
    from autocog.bench.workers import RemoteWorker

    probed = [RemoteWorker(u) for u in workers]
    urls = [w.url for w in probed if tag in w.capabilities()["models"]]
    if not urls:
        hosted = {w.url: w.capabilities()["models"] for w in probed}
        raise autocog.errors.ConfigError(
            f"no worker hosts model {tag!r}: {hosted}")
    return urls


def run_quality(model=None, data=None, formatter="", questions=0,
                syntaxes=None, demos=None, out=".", root=None, seed=42,
                n_ctx=2048, workers=None, log=print):
    """Run the quality matrix; returns the list of events."""
    root = root or find_root(os.getcwd())
    if root is None:
        raise autocog.errors.ConfigError("cannot locate the repo root (share/syntax)")
    data = data or os.path.join(root, "share", "benchmarks", "quality", "questions.json")
    tag = model_tag(model)
    os.makedirs(out, exist_ok=True)

    if workers:
        from autocog.remote import lane_ledger

        urls = compatible_urls(workers, tag)
        log(f"workers: {len(urls)} hosting {tag!r}")
        qrun = QualityRun(tag, data, out, root, formatter=formatter,
                          questions=questions, syntaxes=syntaxes,
                          demos=demos, log=log)
        if not qrun.pending:
            log("all samples present — nothing to do")
            return []
        ledger = lane_ledger(urls)
        pools = qrun.make_pools(urls, ledger)
        for pool in pools.values():
            pool.set_seed(seed)         # rng runs; real models are deterministic
        asyncio.run(run_samples([(qrun, pools)], {tag: ledger}, log=log))
        return [ev for ev in (qrun._writer.events if qrun._writer else [])]

    if model and not os.path.isfile(model):
        raise autocog.errors.ConfigError(
            f"model {model!r} is a tag, not a file — tags need --worker")

    # Local in-process path: one lane, the original sequential matrix.
    worker = LocalWorker(model=model, n_ctx=n_ctx)
    qs = load_questions(data, load_formatter(formatter), limit=questions)
    syntaxes = syntaxes or ["complete", "stripped"]
    demos = demos or ["select"]
    mcq_dir = os.path.join(root, "share", "demos", "mcq")
    search = os.path.join(root, "share", "search", "default.json")
    stamp = f"{results.host_name()}-{tag}"
    nd = results.NdjsonWriter(os.path.join(out, f"results-{stamp}.ndjson"),
                              atomic=False)
    programs = {demo: autocog.compile(os.path.join(mcq_dir, f"{demo}.stl"),
                                      includes=[mcq_dir])
                for demo in demos}

    base = {
        "log.logger": "autocog.bench.quality",
        "event.action": "quality.run",
        "autocog.bench.model": tag,
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
                                            question=q["question"],
                                            choices=choices_for(demo, q["choices"]))
                    except Exception as e:  # a failing pair is a datapoint
                        result, error = None, f"{type(e).__name__}: {e}"
                    wall = time.time() - t0

                    answer, correct = score_answer(demo, extract_answer(result), q)
                    ev = results.base_event("autocog.bench.quality", "quality.run", dict(base))
                    ev.update({
                        "autocog.bench.syntax": syntax,
                        "autocog.bench.demo": demo,
                        "autocog.bench.question": q["id"],
                        "autocog.bench.n_choices": len(q["choices"]),
                        "autocog.bench.wall_seconds": round(wall, 3),
                        "autocog.bench.answer": answer,
                        "autocog.bench.correct": correct,
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
    results.quality_md(md, f"{results.host_name()} — {tag}", nd.events)
    log(f"\nresults: {nd.path}\n         {md}")
    return nd.events
