"""Deciding the worker topology.

A descriptor never declares how many workers to run: that is a property of
the machine, and the machine is measured. With one GPU per worker and lanes
sized to saturate compute, the only question left is whether the campaign's
models fit alongside each other or have to take turns.

    isolated      one worker holding every model the campaign names
    distributed   one worker per GPU, models spread across them

The choice follows from VRAM against the weights, so a campaign descriptor
moves between machines unedited.
"""

import os


class Slot:
    """One worker: a GPU (or none) and the models it hosts."""

    def __init__(self, index, gpu, models):
        self.index = index
        self.gpu = gpu
        self.models = list(models)

    @property
    def port(self):
        return 17700 + self.index

    @property
    def url(self):
        return f"127.0.0.1:{self.port}"

    def hosts(self, tag):
        return tag == "rng" or any(m["tag"] == tag for m in self.models)

    def as_dict(self):
        return {"index": self.index, "gpu": self.gpu, "url": self.url,
                "models": self.models}


def model_bytes(layout, model):
    if model.get("pseudo"):
        return 0
    path = os.path.join(layout.models, model["file"])
    try:
        return os.path.getsize(path)
    except OSError:
        return 0


def plan(descriptor, layout, machine, mode=None):
    """Assign the campaign's models to workers. Returns a list of Slot.

    `machine` is the probe's report: {"gpus": [{"index", "total_bytes"}, ...]}.
    A GPU-less machine gets one CPU worker holding everything.
    """
    models = descriptor.models
    gpus = machine.get("gpus") or []

    if not gpus:
        return [Slot(0, None, models)]

    total = sum(model_bytes(layout, m) for m in models)
    smallest = min(g["total_bytes"] for g in gpus)
    # Weights are the floor, not the whole cost: KV and activations need room
    # too, so a campaign only shares one worker when its models fit in well
    # under half the card.
    fits_together = total and total < smallest * 0.45

    if mode == "isolated" or (mode is None and fits_together):
        return [Slot(i, g["index"], models) for i, g in enumerate(gpus)]

    slots = [Slot(i, g["index"], []) for i, g in enumerate(gpus)]
    for i, model in enumerate(sorted(models, key=lambda m: -model_bytes(layout, m))):
        slots[i % len(slots)].models.append(model)
    return [s for s in slots if s.models]


def unhosted(slots, descriptor):
    """Model tags a run wants that no slot can serve — a campaign that would
    stall partway through, reported before anything is spawned."""
    wanted = {run["model"] for run in descriptor.runs}
    return sorted(t for t in wanted if not any(s.hosts(t) for s in slots))
