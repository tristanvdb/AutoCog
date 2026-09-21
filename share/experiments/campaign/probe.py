"""Measuring the machine, and checking the campaign can actually run on it.

Two different things share this stage, and it is worth keeping them apart:

  * Capacity probing — how much VRAM, how many GPUs, roughly how fast. Its
    output is a scheduling decision. It is never a published number, and it
    runs on whatever machine was cheapest today.

  * The validity gate — a few real samples, checked for answers that are
    *usable*, not merely present.

The gate exists because the previous one did not catch anything that mattered.
It asserted that samples came back scored and error-free, which a run can do
while being completely wrong: in the last campaign 17% of label answers were
letters naming no choice, every one of them the same letter, and the gate
passed. An out-of-range answer is scored (as a miss), so "no errors" was
never the property worth asserting.

So: on the real model, on a handful of samples, are the answers in range, and
do they vary? Seconds to run, before any money is spent.
"""

import collections
import json
import os
import subprocess


def scan_machine():
    """GPU inventory. Returns {"gpus": [{"index", "name", "total_bytes"}]}."""
    try:
        out = subprocess.run(
            ["nvidia-smi", "--query-gpu=index,name,memory.total",
             "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return {"gpus": []}
    if out.returncode != 0:
        return {"gpus": []}
    gpus = []
    for line in out.stdout.strip().splitlines():
        parts = [p.strip() for p in line.split(",")]
        if len(parts) != 3:
            continue
        try:
            gpus.append({"index": int(parts[0]), "name": parts[1],
                         "total_bytes": int(float(parts[2])) * 1024 * 1024})
        except ValueError:
            continue
    return {"gpus": gpus}


def load_profile(layout):
    path = os.path.join(layout.calibration, "machine.json")
    if os.path.isfile(path):
        try:
            return json.load(open(path))
        except (OSError, json.JSONDecodeError):
            pass
    return None


def save_profile(layout, machine):
    os.makedirs(layout.calibration, exist_ok=True)
    path = os.path.join(layout.calibration, "machine.json")
    with open(path, "w") as f:
        json.dump(machine, f, indent=2)
    return path


class GateFailure(Exception):
    pass


def check_answers(events, questions, minimum=3):
    """The validity gate over one run's events. Raises GateFailure.

    In range: the answer names a choice the sample offered. Varied: the run
    did not return the same answer every time, which is what a degenerate
    decode looks like from the outside — and what a pass/fail-only gate reads
    as success.
    """
    by_id = {q["id"]: q for q in questions}
    answers = collections.Counter()
    scored = 0
    for ev in events:
        if ev.get("error.message"):
            raise GateFailure(f"sample errored: {ev['error.message']}")
        answer = ev.get("autocog.bench.answer")
        q = by_id.get(ev.get("autocog.bench.question"))
        if answer is None:
            raise GateFailure(
                f"no answer for {ev.get('autocog.bench.question')!r} "
                f"({ev.get('autocog.bench.program')})")
        if q is not None:
            allowed = {c["value"] for c in q["choice"]}
            if answer not in allowed:
                raise GateFailure(
                    f"{ev.get('autocog.bench.program')} answered {answer!r}, "
                    f"which names no choice of {ev['autocog.bench.question']!r} "
                    f"— the answer is unusable, not merely wrong")
        answers[answer] += 1
        scored += 1

    if scored < minimum:
        raise GateFailure(f"only {scored} sample(s) scored, wanted {minimum}")
    if len(answers) == 1 and scored > 2:
        answer, n = answers.most_common(1)[0]
        raise GateFailure(
            f"every one of {n} samples answered {answer!r} — a decode that "
            f"ignores the question reads as an accuracy, not as an error")
    return {"scored": scored, "distinct": len(answers)}
