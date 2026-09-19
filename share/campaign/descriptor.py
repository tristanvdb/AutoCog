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
      - {name: arc, source: arc, split: test, samples: 200}

    programs:
      record-label:
        filepath: demos/mcq/record-label.stl
        options: []
        labels: [repeat, record, label]

    syntaxes:
      i1-typed:
        filepath: complete.json          # local dir first, then share/syntax
        prompt_zero_index: false         # every other field OVERRIDES it

    program_groups:
      record: [record-label, record-value]
    syntax_groups:
      index: [complete, i1-typed]

    defaults:
      syntaxes: [complete, special, stripped]

    prior: [screen-base-1b]        # draw questions that campaign did not use

    runs:
      - {name: mechanism-arc, model: 1b, dataset: arc,
         programs: ["@record"], syntaxes: ["@index"]}

`runs` is an ordered list, not a matrix: put the arm you most need first, so
a campaign that dies early still leaves you the comparison you came for.

A `@name` in `programs` or `syntaxes` expands the matching group. Groups
exist because the same list is usually shared by several runs, and a list
repeated is a list that drifts: two runs meant to be one contrast quietly
stop being one when somebody edits one copy.
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
        #: name -> {filepath?, <overrides>}. `filepath` defaults to
        #: "<name>.json", so `syntaxes: [complete]` works with nothing
        #: declared and the mapping is only for overrides and renames.
        self.syntaxes = dict(data.get("syntaxes") or {})
        self.program_groups = dict(data.get("program_groups") or {})
        self.syntax_groups = dict(data.get("syntax_groups") or {})
        self.defaults = dict(data.get("defaults") or {})
        #: earlier campaigns whose QUESTIONS this one must avoid — a name
        #: under the results root, or a path. Resolved by the app, not
        #: here: this module reads the descriptor and nothing about the
        #: machine.
        self.prior = list(data.get("prior") or [])
        self.runs = _runs(data.get("runs") or [], self.defaults,
                          self.program_groups, self.syntax_groups)
        for run in self.runs:
            for name in run["syntaxes"]:
                self.syntaxes.setdefault(name, {})
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

    def syntax(self, name):
        return self.syntaxes.get(name)

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
    single matrix (see share/campaign/samples.py). 0 = the whole dataset.

    There is no `limit`. It used to cap the BUILT file, from back when a
    run took the dataset whole; with `samples` doing the drawing its only
    effect was to shrink the pool `prior` carves disjoint campaigns out of
    -- a 1000-item cap is ten campaigns where the full MMLU test is a
    hundred and forty. It was also a cache footgun: the built file is
    keyed by name alone, so editing the cap silently did nothing."""
    out = []
    for entry in entries:
        if isinstance(entry, str):
            entry = {"name": entry}
        if "name" not in entry:
            raise DescriptorError(f"dataset entry has no name: {entry}")
        out.append({"name": entry["name"], "source": entry.get("source"),
                    "split": entry.get("split"),
                    "samples": int(entry.get("samples", 0) or 0)})
    return out


def _expand(names, groups, what, where):
    """`@group` -> the group's members, everything else verbatim.

    Not recursive: a group of groups reads like an abstraction and behaves
    like an indirection nobody can follow when a run turns out to have the
    wrong arms."""
    out = []
    for name in names:
        if not name.startswith("@"):
            out.append(name)
            continue
        group = groups.get(name[1:])
        if group is None:
            raise DescriptorError(
                f"{where}: no {what} group {name[1:]!r} "
                f"(have {sorted(groups)})")
        for member in group:
            if member.startswith("@"):
                raise DescriptorError(
                    f"{what} group {name[1:]!r} refers to {member!r}: "
                    f"groups do not nest")
            if member not in out:
                out.append(member)
    return out


def _runs(entries, defaults, program_groups=None, syntax_groups=None):
    out = []
    for i, entry in enumerate(entries):
        run = dict(defaults)
        run.update(entry)
        run.setdefault("name", f"run-{i}")
        for key in ("model", "dataset", "programs"):
            if key not in run:
                raise DescriptorError(f"run {run['name']!r} has no {key}")
        run["programs"] = _expand(list(run["programs"]), program_groups or {},
                                  "program", f"run {run['name']!r}")
        run["syntaxes"] = _expand(list(run.get("syntaxes") or ["complete"]),
                                  syntax_groups or {}, "syntax",
                                  f"run {run['name']!r}")
        # Refused rather than ignored: a descriptor carried over from
        # before `samples` moved to the dataset would otherwise run the
        # whole dataset silently, which is a very expensive way to find
        # out a field was dropped.
        if "limit" in run or "samples" in run:
            raise DescriptorError(
                f"run {run['name']!r} sets `samples`/`limit`: the sample "
                f"count belongs to the dataset now, so that every run in a "
                f"campaign draws the same questions and the arms stay "
                f"comparable")
        out.append(run)
    return out
