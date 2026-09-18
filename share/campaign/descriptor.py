"""Reading a campaign descriptor.

YAML when PyYAML is installed, JSON otherwise — YAML is a JSON superset, so
one loader covers both and the filename extension is never consulted.

A descriptor declares what to run and what the run needs; it says nothing
about the machine. How many workers fit, which GPU hosts which model, how
many lanes saturate it — those are measured, not declared (see probe.py and
plan.py). The campaign's name is the descriptor's filename, so there is no
field to disagree with the file it lives in.

    models:
      - {tag: 1b, file: Llama-3.2-1B-Instruct-Q8_0.gguf, ctx: 4096}

    datasets:
      - {name: arc-1000, source: arc, split: test, limit: 1000, samples: 200}

    programs:
      record-label:
        filepath: demos/mcq/record-label.stl
        options: []
        labels: [repeat, record, label]

    defaults:
      syntaxes: [complete, special, stripped]

    prior: [screen-base-1b]        # draw questions that campaign did not use

    runs:
      - {name: mechanism-arc, model: 1b, dataset: arc-1000,
         programs: [record-label, record-value]}

`runs` is an ordered list, not a matrix: put the arm you most need first, so
a campaign that dies early still leaves you the comparison you came for.
"""

import json
import os

#: the built-in pseudo-model — no weights, served by every worker
RNG = "rng"

try:
    import yaml
except ImportError:                                   # pragma: no cover
    yaml = None


class DescriptorError(Exception):
    pass


def load(path):
    """Parse a descriptor. Returns a Descriptor."""
    text = open(path).read()
    if yaml is not None:
        try:
            data = yaml.safe_load(text)
        except yaml.YAMLError as e:
            raise DescriptorError(f"{path}: {e}") from e
    else:
        try:
            data = json.loads(text)
        except json.JSONDecodeError as e:
            raise DescriptorError(
                f"{path}: {e} (install PyYAML to write descriptors in YAML)") from e
    if not isinstance(data, dict):
        raise DescriptorError(f"{path}: a descriptor is a mapping")
    name = os.path.splitext(os.path.basename(path))[0]
    return Descriptor(name, data, path)


class Descriptor:
    def __init__(self, name, data, path=""):
        self.name = name
        self.path = path
        self.data = data
        self.models = _models(data.get("models") or [])
        self.datasets = _datasets(data.get("datasets") or [])
        self.programs = dict(data.get("programs") or {})
        self.defaults = dict(data.get("defaults") or {})
        #: earlier campaigns whose QUESTIONS this one must avoid — a name
        #: under the results root, or a path. Resolved by the app, not
        #: here: this module reads the descriptor and nothing about the
        #: machine.
        self.prior = list(data.get("prior") or [])
        self.runs = _runs(data.get("runs") or [], self.defaults)
        self._validate()

    def _validate(self):
        tags = {m["tag"] for m in self.models}
        names = {d["name"] for d in self.datasets}
        for run in self.runs:
            if run["model"] not in tags and run["model"] != "rng":
                raise DescriptorError(
                    f"run {run['name']!r}: model {run['model']!r} is not in models")
            if run["dataset"] not in names:
                raise DescriptorError(
                    f"run {run['name']!r}: dataset {run['dataset']!r} is not in datasets")
            for program in run["programs"]:
                if program not in self.programs:
                    raise DescriptorError(
                        f"run {run['name']!r}: program {program!r} is not in programs")

    def model(self, tag):
        for m in self.models:
            if m["tag"] == tag:
                return m
        return None

    def dataset(self, name):
        for d in self.datasets:
            if d["name"] == name:
                return d
        return None

    def programs_for(self, run):
        """The programs section a run needs, in the shape bench consumes."""
        return {name: self.programs[name] for name in run["programs"]}

    def labels(self):
        """Every label any program carries — the axes an analysis can slice."""
        seen = []
        for spec in self.programs.values():
            for label in spec.get("labels") or []:
                if label not in seen:
                    seen.append(label)
        return seen


def _models(entries):
    out = []
    for entry in entries:
        if isinstance(entry, str):
            entry = {"tag": entry, "file": entry}
        if "tag" not in entry:
            raise DescriptorError(f"model entry has no tag: {entry}")
        out.append({"tag": entry["tag"], "file": entry.get("file", entry["tag"]),
                    "ctx": int(entry.get("ctx", 4096)),
                    "pseudo": entry["tag"] == RNG})
    return out


def _datasets(entries):
    """`samples` lives HERE, not on a run: a campaign draws its questions
    once per dataset and every run sees the same ones. A run that could
    set its own would make two arms incomparable while looking like a
    single matrix (see share/campaign/samples.py). 0 = the whole dataset."""
    out = []
    for entry in entries:
        if isinstance(entry, str):
            entry = {"name": entry}
        if "name" not in entry:
            raise DescriptorError(f"dataset entry has no name: {entry}")
        out.append({"name": entry["name"], "source": entry.get("source"),
                    "split": entry.get("split"),
                    "limit": int(entry.get("limit", 0) or 0),
                    "samples": int(entry.get("samples", 0) or 0)})
    return out


def _runs(entries, defaults):
    out = []
    for i, entry in enumerate(entries):
        run = dict(defaults)
        run.update(entry)
        run.setdefault("name", f"run-{i}")
        for key in ("model", "dataset", "programs"):
            if key not in run:
                raise DescriptorError(f"run {run['name']!r} has no {key}")
        run["syntaxes"] = list(run.get("syntaxes") or ["complete"])
        # Refused rather than ignored: a descriptor carried over from
        # before `samples` moved to the dataset would otherwise run the
        # whole dataset silently, which is a very expensive way to find
        # out a field was dropped.
        if "samples" in run:
            raise DescriptorError(
                f"run {run['name']!r} sets `samples`: it belongs to the "
                f"dataset now, so that every run in a campaign draws the "
                f"same questions and the arms stay comparable")
        out.append(run)
    return out
