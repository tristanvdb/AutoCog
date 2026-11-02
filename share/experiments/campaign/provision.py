"""Fetching what a descriptor names: model weights and datasets.

Idempotent by construction — everything checks for the artifact first, so
re-running a campaign after a crash costs nothing. Downloads land on a
`.part` file and are renamed on success, so an interrupted fetch is never
mistaken for a complete one.
"""

import os
import subprocess

from . import catalog


class ProvisionError(Exception):
    pass


def fetch_models(descriptor, layout, log=print):
    """Fetch every model the descriptor names. Returns the paths."""
    os.makedirs(layout.models, exist_ok=True)
    paths = []
    for model in descriptor.models:
        if model.get("pseudo"):
            continue                 # rng has no weights to fetch
        dest = os.path.join(layout.models, model["file"])
        if os.path.isfile(dest) and os.path.getsize(dest) > 0:
            log(f"have: {model['file']}")
            paths.append(dest)
            continue
        url = catalog.url_for(model["file"])
        if url is None:
            raise ProvisionError(
                f"model {model['file']!r} is not in the catalog and not in "
                f"{layout.models} — add a catalog entry or put the file there")
        log(f"fetching: {model['file']}")
        part = dest + ".part"
        r = subprocess.run(["curl", "-L", "-C", "-", "--fail", "--retry", "3",
                            "-o", part, url])
        if r.returncode != 0:
            raise ProvisionError(f"download failed: {model['file']}")
        os.replace(part, dest)
        paths.append(dest)
    return paths


def build_datasets(descriptor, layout, log=print):
    """Convert every dataset the descriptor names into the canonical shape.

    Conversion is where labels come from: a choice arrives as
    {label, value}, decided once, rather than being invented at run time by
    whatever happens to be reading the data.
    """
    os.makedirs(layout.datasets, exist_ok=True)
    convert = layout.share("benchmarks", "quality", "convert.py")
    built = []
    for dataset in descriptor.datasets:
        dest = os.path.join(layout.datasets, f"{dataset['name']}.json")
        if os.path.isfile(dest):
            log(f"have: {dataset['name']}")
            built.append(dest)
            continue
        source, split = dataset.get("source"), dataset.get("split")
        if not source:
            raise ProvisionError(
                f"dataset {dataset['name']!r} is not built and names no "
                f"source to build it from")
        raw = os.path.join(layout.datasets, "raw", source, split or "")
        if not os.path.exists(raw):
            raise ProvisionError(
                f"dataset {dataset['name']!r}: no raw data at {raw}")
        # The WHOLE converted split. How many of it a campaign uses is
        # `samples`, drawn per campaign (samples.py); capping the built file
        # would only shrink the pool `prior` carves disjoint campaigns from.
        cmd = ["python3", convert, source, raw, "--out", dest]
        log(f"building: {dataset['name']} ({source})")
        r = subprocess.run(cmd)
        if r.returncode != 0:
            raise ProvisionError(f"conversion failed: {dataset['name']}")
        built.append(dest)
    return built
