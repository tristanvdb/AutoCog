"""Datapoint formatters: raw dataset records -> benchmark questions.

A formatter is a callable `fn(record) -> question | None` (None = skip),
where a question is the questions.json shape:
    {"id", "topic", "question", "choice": [{"label", "value"}], "answer"}

The choices arrive as labelled records — the label produced when the dataset
was converted, never invented at run time. A program that wants only the texts
pulls `choice.value` through a channel.

Specified as `path/to/file.py:function` (the pytest/gunicorn convention)
and applied client-side to every datapoint of `--data`. The default is
the identity (the data already IS questions.json format).
"""

import importlib.util
import json
import os

from autocog.errors import ConfigError


def identity(record):
    return record


def load_formatter(spec):
    """Resolve a `file.py:fn` spec to a callable (None/'' -> identity)."""
    if not spec:
        return identity
    if ":" not in spec:
        raise ConfigError(
            f"formatter spec must be 'file.py:function', got {spec!r}")
    path, name = spec.rsplit(":", 1)
    if not os.path.isfile(path):
        raise ConfigError(f"formatter file not found: {path}")
    module_name = f"autocog_bench_formatter_{abs(hash(os.path.abspath(path)))}"
    module_spec = importlib.util.spec_from_file_location(module_name, path)
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    fn = getattr(module, name, None)
    if not callable(fn):
        raise ConfigError(f"{path} has no callable {name!r}")
    return fn


def stratify(questions, limit):
    """`limit` questions, spread evenly over `topic`.

    Round-robin: the first item of every topic, then the second of every
    topic, and so on until the count is reached. Equal allocation, not
    proportional — MMLU subject sizes span an order of magnitude, and at
    N=100 proportional allocation lets the big subjects crowd out the
    small ones for no reason anybody chose.

    This replaced a STRIDE (`items[int(i * step)]`), which was described as
    stratified and was not. A stride is systematic sampling; it only
    resembled stratification because the converter emits MMLU in
    filename-sorted order, so subjects happen to be contiguous. Two things
    followed from that, and both mattered:

      * Any topic smaller than the step contributed 0 or 1 items. Drawing
        100 from a 1000-item pool is a step of 10, so a subject holding
        seven of them either appeared once or vanished -- and which
        subjects vanished was decided by alphabetical filename order.
      * `exclude` destroyed even the resemblance. Once an earlier
        campaign's questions are removed the pool is no longer evenly
        spaced, so the next stride lands on a different composition. That
        is exactly the case `prior` exists for -- extending a campaign --
        and the two halves were not drawn alike.

    Round-robin has neither problem: every topic is present while any of
    its items remain, and removing one campaign's draw leaves each topic's
    remainder in order, so the next campaign simply continues down the
    list.

    Order of topics, and of items within one, is the order they appear in
    the data: deterministic, and a campaign's question set has to be.
    Returned in DRAW order, so truncating the result (the validity gate
    takes four) is still spread across topics.
    """
    if not limit or limit >= len(questions):
        return questions
    by_topic = {}
    for q in questions:
        by_topic.setdefault(q.get("topic") or "", []).append(q)
    out = []
    for rank in range(max(len(v) for v in by_topic.values())):
        for items in by_topic.values():
            if rank < len(items):
                out.append(items[rank])
                if len(out) == limit:
                    return out
    return out


def load_questions(data_path, formatter=None, limit=0, exclude=None):
    """Load datapoints (.json list or .jsonl lines), apply the formatter,
    and take `limit` of them stratified by topic (0 = all).

    `exclude` is a set of question digests (see bench.reuse) to drop BEFORE
    sampling, so a campaign asked to avoid an earlier one's questions still
    gets its full `limit` — drawn from what is left, not short by however
    many collided.
    """
    fn = formatter or identity
    records = []
    if data_path.endswith(".jsonl"):
        with open(data_path) as f:
            records = [json.loads(line) for line in f if line.strip()]
    else:
        records = json.load(open(data_path))
    questions = [q for q in (fn(r) for r in records) if q is not None]
    for q in questions:
        for key in ("id", "question", "choice", "answer"):
            if key not in q:
                raise ConfigError(f"formatted question missing {key!r}: {q}")
    if exclude:
        from .reuse import question_digest

        questions = [q for q in questions if question_digest(q) not in exclude]
    return stratify(questions, limit)
