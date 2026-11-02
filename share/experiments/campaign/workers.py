"""Spawning and reaping the workers a plan asks for.

`autocog bench` never does this: it consumes workers by URL and knows nothing
about how they came to exist. Provisioning is the app's job, which is why the
app is here in share/ rather than in the library.
"""

import os
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request


class WorkerFailed(Exception):
    pass


class Workers:
    """The processes behind a plan's slots, as a context manager: whatever
    happens inside, the machine is left without stray workers holding VRAM."""

    def __init__(self, slots, layout, ngl=99, log=print):
        self.slots = slots
        self.layout = layout
        self.ngl = ngl
        self.log = log
        self.procs = []

    def __enter__(self):
        try:
            self._spawn_all()
        except BaseException:
            self.stop()
            raise
        return self

    def __exit__(self, *exc):
        self.stop()
        return False

    def _spawn_all(self):
        for slot in self.slots:
            self._spawn(slot)
        for slot, proc in zip(self.slots, self.procs):
            self._wait_ready(slot, proc)

    def _spawn(self, slot):
        cmd = [sys.executable, "-m", "autocog", "backend",
               "--host", "127.0.0.1", "--port", str(slot.port)]
        for model in slot.models:
            if model.get("pseudo"):
                continue             # a worker serves rng without being told
            cmd += ["--model", _spec(self.layout, model, self.ngl)]
        env = dict(os.environ)
        if slot.gpu is not None:
            # Each GPU backend reads its own variable; the others ignore it.
            env["CUDA_VISIBLE_DEVICES"] = str(slot.gpu)
            env["HIP_VISIBLE_DEVICES"] = str(slot.gpu)
            env["GGML_VK_VISIBLE_DEVICES"] = str(slot.gpu)
        log_path = os.path.join(self.layout.results, "workers",
                                f"worker-{slot.index}.log")
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        sink = open(log_path, "w")
        self.log(f"worker {slot.index}: gpu={slot.gpu} "
                 f"models={[m['tag'] for m in slot.models] or ['rng']} -> {log_path}")
        self.procs.append(subprocess.Popen(cmd, stdout=sink, stderr=sink,
                                           env=env, start_new_session=True))

    def _wait_ready(self, slot, proc, timeout=900):
        """Poll /capabilities, but fail the moment the process dies: a worker
        that OOMs on model load should not cost the timeout before saying so."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if proc.poll() is not None:
                raise WorkerFailed(
                    f"worker {slot.index} exited with {proc.returncode} before "
                    f"serving; see results/workers/worker-{slot.index}.log")
            try:
                with urllib.request.urlopen(
                        f"http://{slot.url}/capabilities", timeout=5):
                    self.log(f"worker {slot.index}: ready at {slot.url}")
                    return
            except (urllib.error.URLError, OSError):
                time.sleep(1.0)
        raise WorkerFailed(f"worker {slot.index} not ready after {timeout}s")

    def urls_for(self, tag):
        return [s.url for s in self.slots if s.hosts(tag)]

    def stop(self):
        for proc in self.procs:
            if proc.poll() is None:
                try:
                    os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
                except OSError:
                    proc.terminate()
        for proc in self.procs:
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                proc.kill()
        self.procs = []


def _spec(layout, model, ngl):
    import json

    return json.dumps({"path": os.path.join(layout.models, model["file"]),
                       "tag": model["tag"], "ctx": model["ctx"], "ngl": ngl})
