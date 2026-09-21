#!/usr/bin/env python3
"""What the model was actually shown, and what it actually wrote.

    python3 share/experiments/transcript.py <descriptor> --out DIR
        [--model rng|<tag> --gguf PATH] [--program P]... [--syntax S]...
        [--questions N]

Everything else in this repo reads a run as numbers: accuracy, friction,
token counts, a decision distribution. None of that tells you whether the
question put to the model made sense. This prints the rendered prompt with
the model's own emissions marked, one file per (program, syntax, question),
so a person can read it.

It exists because the expensive defects have all been visible this way and
invisible in the metrics. ARC choices already carry letters, so `select`
was asking the model to translate a visible letter into a position --
that is one glance at a transcript and nothing in an accuracy table. And
`select` emits a POSITION whose spelling the syntax controls, which cost
88 of a 332-arm campaign before a preflight caught the 42 that crashed;
the other 46 returned a plausible wrong answer, and a transcript would
have shown the candidates as `1 2 3 4` against choices numbered from 0.

    «...»   emitted by the model
    plain   rendered by the instantiator

The decision block under each step is the whole candidate distribution,
which a `choose` keeps for free: pruning MARKS a sibling rather than
dropping it, and ranking had to score them all. `logprobs` holds NLL, so
the winner is the SMALLEST, and these are exp(-NLL) normalised PER TOKEN
-- see distribution(), where summing instead would misread every
branch whose candidates differ in length.

On `rng` the words are noise -- the point is the structure: what was
asked, where the model was allowed to write, which candidates it was
offered. Run it against a real model for a real tokenizer.
"""

import argparse
import json
import math
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

MARK_OPEN, MARK_CLOSE = "«", "»"
RULE = "─" * 72


def chosen_path(node):
    """The un-pruned spine of an FTT, with the decisions met on the way.

    A `choose` leaves every candidate in the tree and marks the losers, so
    the branch point and the alternatives are both still here."""
    steps, decisions = [], []
    while node is not None:
        steps.append(node)
        kids = node.get("children") or []
        groups = {}
        for k in kids:
            groups.setdefault(k["uid"], []).append(k)
        nxt = None
        for uid, sibs in groups.items():
            live = next((s for s in sibs if not s.get("pruned")), sibs[0])
            if len(sibs) > 1:
                decisions.append((node.get("text", ""), sibs, live))
            if nxt is None:
                nxt = live
        node = nxt
    return steps, decisions


def distribution(sibs):
    """Normalised exp(-NLL) per candidate, PER TOKEN.

    NLL: positive is cost, and the winner is the smallest -- softmaxing it
    directly inverts the ranking.

    Per token, not summed, because candidates at one branch point are NOT
    the same length. An array's continuation choice is `work[1]:` (6
    tokens) against `answer:` (4): summing makes the shorter one look
    overwhelmingly preferred at any per-token cost, so a transcript would
    show the search taking a p=0.000 option five times in a row. Measured
    on the tiny model those two differ by 0.03 nats per token and by 23
    nats summed. The same applies to `repeat`, whose candidates are choice
    texts of different lengths.
    """
    nlls = [sum(s["logprobs"]) / max(len(s["tokens"]), 1) for s in sibs]
    lo = min(nlls)
    # Shift before exp: raw NLLs run to tens of nats and underflow.
    w = [math.exp(-(n - lo)) for n in nlls]
    total = sum(w) or 1.0
    return [x / total for x in w]


def render(ftt):
    """(text with emissions marked, decision lines)."""
    steps, decisions = chosen_path(ftt)
    out = []
    for node in steps:
        text = node.get("text", "")
        if not text:
            continue
        out.append(f"{MARK_OPEN}{text}{MARK_CLOSE}"
                   if node.get("field") is not None else text)
    lines = []
    for label, sibs, live in decisions:
        probs = distribution(sibs)
        shown = sorted(zip(sibs, probs), key=lambda p: -p[1])[:8]
        cand = "  ".join(
            f"{s.get('text','')!r}:{p:.3f}"
            + ("*" if s is live else ("~" if s.get("pruned") else ""))
            for s, p in shown)
        lines.append(f"  {label.strip() or '(unlabelled)'}   n={len(sibs)}  "
                     f"chose {live.get('text','')!r}\n      {cand}")
    return "".join(out), lines


def knobs(syntax):
    """The rendering knobs, so a transcript says which cell it is."""
    keys = ("prompt_with_index", "prompt_zero_index", "prompt_with_format",
            "prompt_indent", "desc_inline")
    return "  ".join(f"{k}={syntax.get(k)!r}" for k in keys if k in syntax)


def transcribe(program, engine, externals, question, out):
    """Run one sample and write its transcript. Returns the answer."""
    import autocog
    from autocog.recorder import Recorder

    with tempfile.TemporaryDirectory() as tmp:
        rec = Recorder(kinds={"ftt"}, path=tmp)
        try:
            answer = engine.run(program, externals=externals, recorder=rec,
                                topic=question.get("topic", ""),
                                question=question["question"],
                                choice=question["choice"])
        except Exception as e:                       # a dead arm is readable
            answer = f"<{type(e).__name__}: {e}>"
        ftts = sorted(os.path.join(d, f)
                      for d, _, fs in os.walk(tmp) for f in fs
                      if f.endswith(".ftt.json"))
        blocks = []
        for i, path in enumerate(ftts, 1):
            prompt = os.path.basename(os.path.dirname(path))
            text, decisions = render(json.load(open(path)))
            blocks.append(f"{RULE}\n step {i}/{len(ftts)}   prompt {prompt}\n"
                          f"{RULE}\n{text}\n"
                          + ("\n decisions\n" + "\n".join(decisions) + "\n"
                             if decisions else ""))
    out.write("\n".join(blocks))
    return answer


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("descriptor", help="campaign descriptor (YAML or JSON)")
    ap.add_argument("--out", required=True, help="directory for transcripts")
    ap.add_argument("--gguf", default=None,
                    help="model file (default: the rng pseudo-model)")
    ap.add_argument("--program", action="append", default=None,
                    help="only these programs (repeatable)")
    ap.add_argument("--syntax", action="append", default=None,
                    help="only these syntaxes (repeatable; default: the "
                         "first one each program's run names)")
    ap.add_argument("--questions", type=int, default=1)
    ap.add_argument("--data", default=os.path.join(
        REPO, "share", "benchmarks", "quality", "questions.json"))
    args = ap.parse_args()

    import autocog
    from autocog.bench.formatters import load_questions
    from autocog.externals import load_externals
    from campaign.descriptor import load
    from campaign.layout import Layout
    from campaign import runner, syntaxes as syntaxing

    desc = load(args.descriptor)
    layout = Layout(REPO, os.path.dirname(os.path.abspath(args.descriptor)))
    resolved, _ = syntaxing.materialize_all(
        desc, layout, os.path.join(args.out, ".syntax"), log=lambda *_: None)

    # Which (program, syntax) pairs: the descriptor's own pairings, so a
    # transcript is of an arm the campaign will actually run.
    arms, seen = [], set()
    for run in desc.runs:
        cells = args.syntax or run["syntaxes"][:1]
        for name in run["programs"]:
            if args.program and name not in args.program:
                continue
            for cell in cells:
                if cell not in run["syntaxes"] or (name, cell) in seen:
                    continue
                seen.add((name, cell))
                arms.append((name, cell))

    questions = load_questions(args.data, limit=args.questions)
    os.makedirs(args.out, exist_ok=True)
    search = os.path.join(REPO, "share", "search", "default.json")
    tag = os.path.basename(args.gguf) if args.gguf else "rng"
    index = []

    for name, cell in arms:
        spec = runner.program_specs(desc, layout, {"programs": [name]})[name]
        program = autocog.compile(
            spec["filepath"],
            includes=[os.path.join(REPO, "share", "library", "stlib")])
        externals = load_externals(program)
        engine = autocog.Engine(model=args.gguf, syntax=resolved[cell],
                                search=search, n_ctx=2048)
        syntax = json.load(open(resolved[cell]))
        path = os.path.join(args.out, f"{name}__{cell}.txt")
        with open(path, "w") as f:
            f.write(f"{name}   syntax {cell}   model {tag}\n{knobs(syntax)}\n\n")
            for q in questions:
                f.write(f"\n### question {q['id']}   gold {q['answer']!r}\n\n")
                answer = transcribe(program, engine, externals, q, f)
                f.write(f"\n resolved answer: {answer!r}   "
                        f"{'HIT' if answer == q['answer'] else 'miss'}\n")
        index.append(f"  {name:<22} {cell:<26} -> {os.path.basename(path)}")
        print(index[-1])

    with open(os.path.join(args.out, "INDEX.txt"), "w") as f:
        f.write(f"transcripts   model {tag}   {len(arms)} arm(s)   "
                f"{len(questions)} question(s)\n\n" + "\n".join(index) + "\n")
    print(f"\n{len(arms)} transcript(s) -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
