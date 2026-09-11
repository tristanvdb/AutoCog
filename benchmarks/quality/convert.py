#!/usr/bin/env python3
"""Convert public MCQ datasets to the questions.json format.

    python3 convert.py arc  <ARC-*-Test.jsonl>  [--limit N] [--out FILE]
    python3 convert.py mmlu <mmlu/test dir>     [--limit N] [--out FILE]

Emits `[{id, topic, question, choices[4], answer}, ...]` (see README).
Only clean 4-choice items with a resolvable answer are kept (ARC has a few
3/5-choice questions and numeric labels; MMLU is uniformly 4-choice).
--limit takes a deterministic stratified sample (every k-th item of the
full ordered set) so a small run still spans topics/subjects.
"""

import argparse
import csv
import glob
import json
import os
import sys


def load_arc(path):
    out = []
    for line in open(path):
        q = json.loads(line)
        choices = q["question"]["choices"]
        if len(choices) != 4:
            continue
        labels = [c["label"] for c in choices]
        texts = [c["text"] for c in choices]
        if q.get("answerKey") not in labels or len(set(texts)) != 4:
            continue
        out.append({
            "id": q["id"],
            "topic": "Science",
            "question": q["question"]["stem"],
            "choices": texts,
            "answer": texts[labels.index(q["answerKey"])],
        })
    return out


def load_mmlu(test_dir):
    out = []
    for path in sorted(glob.glob(os.path.join(test_dir, "*_test.csv"))):
        subject = os.path.basename(path)[: -len("_test.csv")].replace("_", " ")
        with open(path, newline="") as f:
            for i, row in enumerate(csv.reader(f)):
                if len(row) != 6 or row[5] not in "ABCD":
                    continue
                choices = row[1:5]
                if len(set(choices)) != 4:
                    continue
                out.append({
                    "id": f"{subject.replace(' ', '-')}-{i}",
                    "topic": subject,
                    "question": row[0],
                    "choices": choices,
                    "answer": choices["ABCD".index(row[5])],
                })
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dataset", choices=["arc", "mmlu"])
    ap.add_argument("path", help="ARC test JSONL / MMLU test csv directory")
    ap.add_argument("--limit", type=int, default=0, help="stratified sample size (0 = all)")
    ap.add_argument("--out", default="", help="output file (default: stdout)")
    args = ap.parse_args()

    items = load_arc(args.path) if args.dataset == "arc" else load_mmlu(args.path)
    if not items:
        sys.exit(f"no usable questions found in {args.path}")
    if args.limit and args.limit < len(items):
        step = len(items) / args.limit
        items = [items[int(i * step)] for i in range(args.limit)]

    text = "[\n" + ",\n".join("  " + json.dumps(q, ensure_ascii=False) for q in items) + "\n]\n"
    if args.out:
        with open(args.out, "w") as f:
            f.write(text)
        print(f"{len(items)} questions -> {args.out}", file=sys.stderr)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
