"""Where a campaign's artifacts live.

Everything sits beside the checkout, never inside it — a campaign writes no
file into the repository, and a clone stays a clone:

    ~/my-nfs/                 <- WORKDIR (where you invoke from)
      autocog/                <- the checkout
      venv/  build/  ccache/  models/  datasets/  calibration/  results/

No hidden directories: an operator inspecting a rented box should see what is
there without being told to look for dotfiles.
"""

import os


class Layout:
    """The directories a campaign reads and writes.

    `workdir` defaults to the invocation directory; invoking from inside the
    checkout falls back to the checkout's parent, so a campaign run from a
    development tree still keeps its artifacts out of it.
    """

    def __init__(self, repo, workdir=None):
        self.repo = os.path.abspath(repo)
        if workdir:
            self.workdir = os.path.abspath(workdir)
        else:
            cwd = os.path.abspath(os.getcwd())
            inside = cwd == self.repo or cwd.startswith(self.repo + os.sep)
            self.workdir = os.path.dirname(self.repo) if inside else cwd

    def __repr__(self):
        return f"Layout(repo={self.repo!r}, workdir={self.workdir!r})"

    def _dir(self, name, env=None):
        override = os.environ.get(env) if env else None
        return os.path.abspath(override) if override \
            else os.path.join(self.workdir, name)

    @property
    def venv(self):
        return self._dir("venv")

    @property
    def build(self):
        return self._dir("build")

    @property
    def ccache(self):
        return self._dir("ccache", "CCACHE_DIR")

    @property
    def models(self):
        return self._dir("models", "MODELS_PATH")

    @property
    def datasets(self):
        return self._dir("datasets", "DATASETS_PATH")

    @property
    def calibration(self):
        return self._dir("calibration")

    @property
    def results(self):
        return self._dir("results", "RESULTS_PATH")

    def results_for(self, name):
        """A campaign's own results directory: results/<descriptor basename>."""
        return os.path.join(self.results, name)

    def share(self, *parts):
        """A path inside the checkout's share/ — programs, syntaxes, cells."""
        return os.path.join(self.repo, "share", *parts)

    def ensure(self):
        for path in (self.models, self.datasets, self.calibration, self.results):
            os.makedirs(path, exist_ok=True)
        return self
