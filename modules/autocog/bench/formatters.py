"""Datapoint formatters: raw dataset records -> benchmark questions.

A formatter is a callable `fn(record) -> question | None` (None = skip),
where a question is the questions.json shape:
    {"id", "topic", "question", "choices", "answer"}

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


def load_questions(data_path, formatter=None, limit=0):
    """Load datapoints (.json list or .jsonl lines), apply the formatter,
    and take a deterministic stratified sample of `limit` (0 = all)."""
    fn = formatter or identity
    records = []
    if data_path.endswith(".jsonl"):
        with open(data_path) as f:
            records = [json.loads(line) for line in f if line.strip()]
    else:
        records = json.load(open(data_path))
    questions = [q for q in (fn(r) for r in records) if q is not None]
    for q in questions:
        for key in ("id", "question", "choices", "answer"):
            if key not in q:
                raise ConfigError(f"formatted question missing {key!r}: {q}")
    if limit and limit < len(questions):
        step = len(questions) / limit
        questions = [questions[int(i * step)] for i in range(limit)]
    return questions
