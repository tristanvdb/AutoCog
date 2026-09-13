#!/usr/bin/env python3
"""Choice-scoring adjudication probe.

One evaluation per question with `enum.width` large enough to keep every
candidate branch in the FTT; the per-candidate forced NLLs, token counts
and byte counts are extracted from the tree. Scoring rules are then
compared OFFLINE from the same runs — the engine's ranking metric only
decides which branch the tree expands, which MCQ answers do not depend
on. Rules adjudicated:

    sum    argmin total NLL            (joint probability; short bias)
    mean   argmin NLL / token count    (per-token geometric mean)
    bytes  argmin NLL / byte count     (tokenizer-agnostic)
    bayes  argmin NLL - b*tokens       (Oostermeijer 2026: subtract the
           fitted linear length trend; b estimated per group by the
           within-question centred fixed-effect slope)

Length bias per rule is the mean within-question Kendall tau between
candidate token counts and candidate scores (same formalization as the
paper), so the bias of every rule is measured, not assumed.

    probe_choices.py run --demo select --data q.json --out probe.ndjson
        [--formatter f.py:fn] [--limit N] [--syntax name-or-path]
        [--model path.gguf | --rng] [--worker host:port] [--ctx N]
    probe_choices.py analyze probe1.ndjson [probe2.ndjson ...] --out report
"""

import argparse
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def resolve_syntax(name):
    if os.path.isfile(name):
        return name
    return os.path.join(REPO, "share", "syntax", f"{name}.json")


def candidate_group(node):
    """The choice field's candidates: the unique node whose 2+ children
    all carry the same field id (MCQ demos have one branching field)."""
    kids = node.get("children", [])
    fields = [c.get("field") for c in kids]
    if len(kids) >= 2 and len(set(fields)) == 1 and fields[0] is not None:
        return kids
    for c in kids:
        found = candidate_group(c)
        if found:
            return found
    return None


def run(args):
    import autocog
    from autocog.bench.formatters import load_formatter, load_questions
    from autocog.runtime.sta import runtime_sta_cxx as rt

    questions = load_questions(args.data, load_formatter(args.formatter),
                               limit=args.limit)
    syntax = resolve_syntax(args.syntax)
    search = json.load(open(os.path.join(REPO, "share", "search", "default.json")))
    search["enum"]["width"] = 16   # keep every candidate branch (max 8 choices)
    sid = rt.read_search(json.dumps(search))
    syn_id = rt.load_syntax(syntax)

    mcq = os.path.join(REPO, "share", "demos", "mcq")
    prog = autocog.compile(os.path.join(mcq, f"{args.demo}.stl"), includes=[mcq])

    remote = None
    if args.worker:
        from autocog.remote import RemoteBackend
        remote = RemoteBackend("http://" + args.worker
                               if "://" not in args.worker else args.worker)
        remote.model_tag = (os.path.splitext(os.path.basename(args.model))[0]
                            if args.model else None)
        model_tag = remote.model_tag or "rng"
    else:
        from autocog.backend.llama import backend_llama_cxx as be
        model_id = be.create(args.model, args.ctx) if args.model else 0
        be.set_seed(model_id, 42)
        model_tag = (os.path.splitext(os.path.basename(args.model))[0]
                     if args.model else "rng")

    n_rows, n_skip = 0, 0
    with open(args.out, "w") as sink:
        for q in questions:
            content = {"topic": q.get("topic", ""), "question": q["question"],
                       "choices": q["choices"]}
            fid = rt.instantiate(prog.id, "main", content, syn_id, sid)
            try:
                if remote is not None:
                    ftt = remote._evaluate_remote(rt.get_fta(fid))["ftt"]
                else:
                    ftt_id, _ = be.evaluate(model_id, fid)
                    ftt = rt.get_ftt(ftt_id)
                    rt.release_ftt(ftt_id)
            finally:
                rt.release_fta(fid)

            cands = candidate_group(ftt)
            if not cands or len(cands) != len(q["choices"]):
                n_skip += 1
                continue
            # select candidates are index digits; repeat candidates are the
            # choice texts. Map both to choice indices.
            rows = []
            for c in cands:
                text = c["text"]
                if args.demo.startswith("select"):
                    idx = int(text)
                else:
                    idx = q["choices"].index(text) if text in q["choices"] else -1
                rows.append({"i": idx, "nll": round(sum(c["logprobs"]), 6),
                             "n_tok": len(c["logprobs"]),
                             "n_bytes": len(text.encode("utf-8"))})
            if sorted(r["i"] for r in rows) != list(range(len(q["choices"]))):
                n_skip += 1
                continue
            sink.write(json.dumps({
                "model": model_tag, "demo": args.demo,
                "syntax": os.path.splitext(os.path.basename(syntax))[0],
                "qid": q["id"], "gold": q["choices"].index(q["answer"]),
                "candidates": rows}) + "\n")
            n_rows += 1
    print(f"probe: {n_rows} rows, {n_skip} skipped -> {args.out}")
    return 0 if n_rows else 1


def kendall_tau(xs, ys):
    """Kendall tau-a over a handful of candidates (n <= 8)."""
    n, num, den = len(xs), 0, 0
    for i in range(n):
        for j in range(i + 1, n):
            a = (xs[i] > xs[j]) - (xs[i] < xs[j])
            b = (ys[i] > ys[j]) - (ys[i] < ys[j])
            if a and b:
                num += a * b
            den += 1
    return num / den if den else 0.0


def fit_length_slope(rows):
    """Within-question centred fixed-effect slope of NLL on token count."""
    sxy = sxx = 0.0
    for r in rows:
        c = r["candidates"]
        mt = sum(x["n_tok"] for x in c) / len(c)
        mn = sum(x["nll"] for x in c) / len(c)
        for x in c:
            sxy += (x["n_tok"] - mt) * (x["nll"] - mn)
            sxx += (x["n_tok"] - mt) ** 2
    return sxy / sxx if sxx else 0.0


def analyze(args):
    rows = []
    for path in args.probes:
        rows += [json.loads(l) for l in open(path)]
    groups = {}
    for r in rows:
        groups.setdefault((r["model"], r["demo"], r["syntax"]), []).append(r)

    def scores(cand, rule, b):
        if rule == "sum":
            return [x["nll"] for x in cand]
        if rule == "mean":
            return [x["nll"] / x["n_tok"] for x in cand]
        if rule == "bytes":
            return [x["nll"] / x["n_bytes"] for x in cand]
        return [x["nll"] - b * x["n_tok"] for x in cand]   # bayes

    rules = ("sum", "mean", "bytes", "bayes")
    report = []
    for key in sorted(groups):
        g = groups[key]
        b = fit_length_slope(g)
        entry = {"model": key[0], "demo": key[1], "syntax": key[2],
                 "n": len(g), "b_hat": round(b, 5)}
        for rule in rules:
            correct, tau = 0, 0.0
            for r in g:
                s = scores(r["candidates"], rule, b)
                pick = r["candidates"][s.index(min(s))]["i"]
                correct += (pick == r["gold"])
                # score for tau on the "higher is better" axis: negate NLL
                tau += kendall_tau([x["n_tok"] for x in r["candidates"]],
                                   [-v for v in s])
            entry[f"acc.{rule}"] = round(correct / len(g), 4)
            entry[f"tau.{rule}"] = round(tau / len(g), 4)
        report.append(entry)

    with open(args.out + ".json", "w") as f:
        json.dump(report, f, indent=1)
    cols = ["model", "demo", "syntax", "n", "b_hat"] + \
           [f"{p}.{r}" for r in rules for p in ("acc", "tau")]
    with open(args.out + ".md", "w") as f:
        f.write("| " + " | ".join(cols) + " |\n")
        f.write("|" + "---|" * len(cols) + "\n")
        for e in report:
            f.write("| " + " | ".join(str(e.get(c, "")) for c in cols) + " |\n")
    print(f"adjudication: {len(report)} group(s) -> {args.out}.md / .json")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--demo", required=True, choices=["select", "repeat"])
    r.add_argument("--data", required=True)
    r.add_argument("--formatter", default="")
    r.add_argument("--limit", type=int, default=0)
    r.add_argument("--syntax", default="complete")
    r.add_argument("--model", default=None)
    r.add_argument("--rng", action="store_true")
    r.add_argument("--worker", default=None)
    r.add_argument("--ctx", type=int, default=2048)
    r.add_argument("--out", required=True)
    a = sub.add_parser("analyze")
    a.add_argument("probes", nargs="+")
    a.add_argument("--out", required=True)
    args = ap.parse_args(argv)
    return run(args) if args.cmd == "run" else analyze(args)


if __name__ == "__main__":
    sys.exit(main())
