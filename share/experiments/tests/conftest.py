"""Fixtures for the harness's own tests.

share/experiments is a separate application: the benchmarking harness, not
part of what AutoCog does. Its tests live beside its code and run on their
own,

    python3 -m pytest share/experiments/tests

so they are not in the repo's pytest job. That job gates the library, and a
descriptor format or a worker launcher failing is not a reason to call the
library broken.

The harness DOES import autocog (it is a client of it), so an installed
autocog is a prerequisite here, the same way it is for using the harness
at all.
"""

import os
import pathlib
import sys

import pytest

HERE = pathlib.Path(__file__).resolve().parent
EXPERIMENTS = HERE.parent
REPO = EXPERIMENTS.parent.parent

# `campaign` is a package under share/, run in place -- it is not installed.
sys.path.insert(0, str(EXPERIMENTS))
sys.path.insert(0, str(HERE))


@pytest.fixture(scope="session")
def repo_root():
    return REPO


@pytest.fixture(autouse=True)
def from_repo_root(monkeypatch):
    """Program filepaths in a descriptor resolve against share/."""
    monkeypatch.chdir(REPO)


def layout_for(repo_root, tmp_path):
    from campaign.layout import Layout

    return Layout(str(repo_root), str(tmp_path / "work"))
