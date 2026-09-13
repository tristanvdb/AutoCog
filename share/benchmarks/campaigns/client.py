#!/usr/bin/env python3
"""Campaign client — run a campaign manifest across level-3 workers.

The manifest stays hardware-agnostic (runs only, no worker topology);
the topology is a property of this invocation:

    # attach to workers someone already started (possibly other hosts)
    python client.py manifest.json --workers host:7701,host:7702

    # spawn one worker per local GPU (autodetected via nvidia-smi),
    # models taken from the manifest's runs, assigned round-robin
    python client.py manifest.json

    # explicit GPU subset
    python client.py manifest.json --gpus 0,2

    # GPU-less testing topology: N RNG-only workers
    python client.py manifest.json --rng 4

Spawned workers are `autocog backend` subprocesses: CUDA_VISIBLE_DEVICES
per GPU, an even slice of the available CPUs pinned per worker (llama's
thread pools are created inside the mask), one model each (+ the always-
available "rng"). Every run is dispatched to a worker hosting its model
tag and executed as an `autocog bench campaign` subprocess over a
single-run manifest with that worker's URL injected — the package's
existing surface does all the real work; this script only orchestrates
processes.

Outputs land in the manifest's campaign directory: the runs' own results,
workers/worker-<i>.log, and logs/<label>.log per run.
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def model_tag(model):
    return os.path.splitext(os.path.basename(model))[0] if model else "rng"


def detect_gpus():
    """GPU indices via nvidia-smi; [] when there is no NVIDIA stack."""
    try:
        out = subprocess.run(["nvidia-smi", "-L"], capture_output=True,
                             text=True, timeout=15)
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return []
    if out.returncode != 0:
        return []
    return list(range(len([l for l in out.stdout.splitlines() if l.strip()])))


def gpu_memory_used(gpu):
    """memory.used (MiB) of one GPU via nvidia-smi; None when unavailable."""
    try:
        out = subprocess.run(
            ["nvidia-smi", "-i", str(gpu), "--query-gpu=memory.used",
             "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=15)
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return None
    if out.returncode != 0:
        return None
    try:
        return int(out.stdout.strip().splitlines()[0])
    except (ValueError, IndexError):
        return None


class GpuUtilSampler:
    """Poll one GPU's utilization in a thread; keeps the peak."""

    def __init__(self, gpu, interval=0.1):
        self.gpu = gpu
        self.interval = interval
        self.peak = None
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True)

    def _loop(self):
        while not self._stop.is_set():
            try:
                out = subprocess.run(
                    ["nvidia-smi", "-i", str(self.gpu),
                     "--query-gpu=utilization.gpu",
                     "--format=csv,noheader,nounits"],
                    capture_output=True, text=True, timeout=10)
                util = int(out.stdout.strip().splitlines()[0])
                if self.peak is None or util > self.peak:
                    self.peak = util
            except Exception:  # noqa: BLE001 — sampling is best-effort
                pass
            time.sleep(self.interval)

    def __enter__(self):
        self._thread.start()
        return self

    def __exit__(self, *exc):
        self._stop.set()
        self._thread.join(timeout=5)


def cpu_slices(n):
    """Split this process's available CPUs into n contiguous slices,
    rendered as taskset-style specs for `autocog backend --cpus`."""
    cpus = sorted(os.sched_getaffinity(0))
    if n <= 0 or len(cpus) < n:
        return [None] * n
    per = len(cpus) // n
    slices = []
    for i in range(n):
        chunk = cpus[i * per:(i + 1) * per] if i < n - 1 else cpus[(n - 1) * per:]
        slices.append(",".join(str(c) for c in chunk))
    return slices


def get_capabilities(url, timeout=5):
    with urllib.request.urlopen(f"http://{url}/capabilities", timeout=timeout) as r:
        return json.loads(r.read())


class Worker:
    """One worker in the topology: either attached (url only) or spawned."""

    def __init__(self, index, url=None, model=None, gpu=None, cpus=None,
                 ctx=4096, kv_slots=None, ngl=None, log_dir=None):
        self.index = index
        self.spawned = url is None
        self.url = url or f"127.0.0.1:{free_port()}"
        self.model = model
        self.gpu = gpu
        self.cpus = cpus
        self.ctx = ctx
        self.kv_slots = kv_slots
        self.ngl = ngl
        self.log_path = (os.path.join(log_dir, f"worker-{index}.log")
                         if log_dir else None)
        self.proc = None
        self.caps = None
        self._log = None
        self.vram_before = None   # MiB on self.gpu, sampled just before spawn

    def spawn(self):
        cmd = [sys.executable, "-m", "autocog", "backend",
               "--host", "127.0.0.1", "--port", self.url.rsplit(":", 1)[1],
               "--ctx", str(self.ctx)]
        cmd += ["--model", self.model] if self.model else ["--rng"]
        if self.cpus:
            cmd += ["--cpus", self.cpus]
        if self.kv_slots is not None:
            cmd += ["--kv-slots", str(self.kv_slots)]
        env = dict(os.environ)
        if self.gpu is not None:
            env["CUDA_VISIBLE_DEVICES"] = str(self.gpu)
            self.vram_before = gpu_memory_used(self.gpu)
        if self.ngl is not None:
            # Without AUTOCOG_NGL the model loads CPU-only by design; the
            # GPU offload of a campaign is decided here, per invocation.
            env["AUTOCOG_NGL"] = str(self.ngl)
        self._log = open(self.log_path, "w")
        self.proc = subprocess.Popen(cmd, env=env, stdout=self._log,
                                     stderr=subprocess.STDOUT)

    def wait_ready(self, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.proc is not None and self.proc.poll() is not None:
                raise RuntimeError(
                    f"worker-{self.index} exited (code {self.proc.returncode}) "
                    f"before ready — see {self.log_path}")
            try:
                self.caps = get_capabilities(self.url)
                return
            except (urllib.error.URLError, OSError):
                time.sleep(0.3)
        raise RuntimeError(f"worker-{self.index} @ {self.url} not ready "
                           f"after {timeout}s"
                           + (f" — see {self.log_path}" if self.log_path else ""))

    def stop(self, grace=10):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=grace)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        if self._log:
            self._log.close()
            self._log = None

    def describe(self):
        kind = "spawned" if self.spawned else "attached"
        return (f"worker-{self.index} [{kind}] @ {self.url} "
                f"models={self.caps['models'] if self.caps else '?'} "
                f"gpu={self.gpu if self.gpu is not None else '-'} "
                f"ngl={self.ngl if self.ngl is not None else '-'} "
                f"cpus={self.cpus or (self.caps or {}).get('cpus', 'all') or 'all'}")


def build_topology(args, models, log_dir):
    """Workers from --workers (attach) or spawn (--rng N / per GPU).

    --per-gpu N co-locates N workers on each GPU. The evaluation loop is
    CPU-bound with the GPU >90% idle (measured), so a single large-VRAM
    card runs the whole model fleet as co-resident workers — the
    single-A100 topology. VRAM is the constraint, not compute.
    """
    if args.workers:
        return [Worker(i, url=u.strip())
                for i, u in enumerate(args.workers.split(",")) if u.strip()]

    if args.rng:
        count, gpus, hosted = args.rng, [None] * args.rng, [None] * args.rng
    else:
        gpus = ([int(g) for g in args.gpus.split(",")] if args.gpus
                else detect_gpus())
        if not gpus:
            sys.exit("no GPUs detected: pass --workers to attach, --gpus to "
                     "force a topology, or --rng N for a testing topology")
        gpus = [g for g in gpus for _ in range(args.per_gpu)]
        count = len(gpus)
        if len(models) > count:
            sys.exit(f"{len(models)} models in the manifest but only {count} "
                     f"worker slot(s) — raise --per-gpu, split the campaign, "
                     f"or add --workers")
        # Round-robin models over workers; spare workers duplicate models
        # so runs sharing a model can proceed in parallel.
        hosted = [models[i % len(models)] if models else None
                  for i in range(count)]

    slices = cpu_slices(count)
    return [Worker(i, model=hosted[i], gpu=gpus[i], cpus=slices[i],
                   ctx=args.ctx, kv_slots=args.kv_slots, ngl=args.ngl,
                   log_dir=log_dir)
            for i in range(count)]


def repo_root():
    """The enclosing repo (share/syntax present) — probe runs need it as cwd."""
    d = os.path.dirname(os.path.abspath(__file__))
    while True:
        if os.path.isdir(os.path.join(d, "share", "syntax")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            return os.getcwd()
        d = parent


def sanity_check(workers, out, log):
    """Pre-campaign gate, one worker at a time:
      * affinity: /capabilities' actual mask == the requested slice;
      * offload:  VRAM delta across the model load vs the GGUF size;
      * compute:  a one-question probe run completes, with the GPU's peak
                  utilization sampled while it runs.
    Returns the list of failure strings (empty = gate passes)."""
    failures = []
    sanity_dir = os.path.join(out, "sanity")
    os.makedirs(sanity_dir, exist_ok=True)

    for w in workers:
        checks = []

        # CPU pinning: the worker reports the mask it actually runs under.
        if w.cpus:
            want = set()
            for part in w.cpus.split(","):
                if "-" in part:
                    lo, hi = part.split("-", 1)
                    want.update(range(int(lo), int(hi) + 1))
                elif part:
                    want.add(int(part))
            got = set((w.caps or {}).get("cpus") or [])
            if got == want:
                checks.append(f"affinity ok ({len(got)} cpus)")
            else:
                checks.append(f"AFFINITY MISMATCH want={sorted(want)} got={sorted(got)}")
                failures.append(f"worker-{w.index}: affinity mismatch")

        # GPU offload: the load must have claimed VRAM comparable to the file.
        if w.gpu is not None and w.model:
            after = gpu_memory_used(w.gpu)
            if w.vram_before is None or after is None:
                checks.append("vram: nvidia-smi unavailable")
                if w.ngl:
                    failures.append(f"worker-{w.index}: cannot verify offload")
            else:
                delta = after - w.vram_before
                size = os.path.getsize(w.model) // (1024 * 1024)
                checks.append(f"vram +{delta}MiB (model file {size}MiB)")
                if w.ngl and delta < 0.5 * size:
                    failures.append(
                        f"worker-{w.index}: NGL={w.ngl} but VRAM grew only "
                        f"{delta}MiB for a {size}MiB model — offload did not happen")
                if not w.ngl and delta > 0.5 * size:
                    checks.append("(unexpected offload without --ngl?)")

        # Compute probe: one question through this worker; sample GPU while
        # it runs. RNG workers probe the rng model (CPU-only by nature).
        probe_cmd = [sys.executable, "-m", "autocog", "bench", "quality",
                     "--worker", w.url, "--questions", "1",
                     "--syntaxes", "complete", "--demos", "select",
                     "--out", os.path.join(sanity_dir, f"worker-{w.index}")]
        if w.model:
            probe_cmd += ["--model", w.model]
        t0 = time.time()
        if w.gpu is not None:
            with GpuUtilSampler(w.gpu) as sampler:
                probe = subprocess.run(probe_cmd, capture_output=True,
                                       text=True, cwd=repo_root(), timeout=600)
            peak = sampler.peak
        else:
            probe = subprocess.run(probe_cmd, capture_output=True,
                                   text=True, cwd=repo_root(), timeout=600)
            peak = None
        wall = time.time() - t0
        if probe.returncode != 0:
            checks.append("PROBE FAILED")
            failures.append(f"worker-{w.index}: probe run failed "
                            f"(see {sanity_dir}) — {probe.stdout[-200:]}")
        else:
            checks.append(f"probe ok {wall:.1f}s")
        if peak is not None:
            checks.append(f"gpu-util peak {peak}%")
            if w.ngl and peak == 0:
                failures.append(f"worker-{w.index}: NGL={w.ngl} but the GPU "
                                f"never left 0% during the probe")

        log(f"[sanity] worker-{w.index}: " + "; ".join(checks))
    return failures


def load_campaign(manifest_path):
    base = os.path.dirname(os.path.abspath(manifest_path))
    manifest = json.load(open(manifest_path))
    name = manifest.get("name", "campaign")
    out = manifest.get("out", os.path.join("results", name))
    if not os.path.isabs(out):
        out = os.path.join(base, out)
    return manifest, base, out


def run_models(manifest, base):
    """Ordered unique model paths declared by the manifest's runs,
    resolved against the manifest directory."""
    seen = []
    for run in manifest.get("runs", []):
        m = run.get("model")
        if m and not os.path.isabs(m) and os.path.exists(os.path.join(base, m)):
            m = os.path.join(base, m)
        if m and m not in seen:
            seen.append(m)
    return seen


def dispatch(manifest_path, manifest, base, out, workers, jobs_log):
    """Route each run to a worker hosting its model; one dispatcher thread
    per worker, that worker's runs sequential. Every run executes as an
    `autocog bench campaign` subprocess over a single-run manifest written
    next to the original (same directory => identical relative-path
    resolution), with the worker's URL injected into the run entry."""
    runs = manifest.get("runs", [])
    queues = {w.index: [] for w in workers}
    failures, lock = [], threading.Lock()

    for i, run in enumerate(runs):
        label = run.get("tag") or run.get("out") or f"run-{i}"
        tag = model_tag(run.get("model"))
        capable = [w for w in workers if tag in (w.caps or {}).get("models", [])]
        if not capable:
            failures.append(label)
            jobs_log(f"!!! {label}: no worker hosts model {tag!r}")
            continue
        target = min(capable, key=lambda w: len(queues[w.index]))
        queues[target.index].append((i, label, run))

    logs_dir = os.path.join(out, "logs")
    os.makedirs(logs_dir, exist_ok=True)

    def drain(worker):
        for i, label, run in queues[worker.index]:
            jobs_log(f"[{label}] -> {worker.describe().split(' ')[0]} @ {worker.url}")
            entry = dict(run)
            entry["workers"] = [worker.url]
            mini = {"name": manifest.get("name", "campaign"), "out": out,
                    "runs": [entry]}
            mini_path = os.path.join(base, f".client-{os.getpid()}-{i}.json")
            log_path = os.path.join(logs_dir, f"{label.replace(os.sep, '_')}.log")
            try:
                with open(mini_path, "w") as f:
                    json.dump(mini, f)
                with open(log_path, "w") as lf:
                    rc = subprocess.run(
                        [sys.executable, "-m", "autocog", "bench", "campaign",
                         mini_path],
                        stdout=lf, stderr=subprocess.STDOUT).returncode
                failed = rc != 0
                if not failed:  # bench campaign exits 0; scan for run failures
                    with open(log_path) as lf:
                        failed = any(line.startswith(("!!!", "[failed]"))
                                     for line in lf)
                if failed:
                    with lock:
                        failures.append(label)
                    jobs_log(f"!!! {label} failed — see {log_path}")
                else:
                    jobs_log(f"[{label}] done")
            finally:
                if os.path.exists(mini_path):
                    os.unlink(mini_path)

    threads = [threading.Thread(target=drain, args=(w,), daemon=True)
               for w in workers if queues[w.index]]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    return failures


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("manifest", help="campaign manifest JSON (runs only; "
                                     "topology comes from this invocation)")
    ap.add_argument("--workers", default=None,
                    help="comma list of running workers host:port to attach "
                         "to (spawns nothing)")
    ap.add_argument("--gpus", default=None,
                    help="comma list of GPU indices to spawn workers on "
                         "(default: autodetect)")
    ap.add_argument("--per-gpu", type=int, default=1,
                    help="workers co-located per GPU (CPU-bound loop, "
                         "GPU mostly idle; bound by VRAM). 6 puts the "
                         "whole model fleet on one A100-80GB")
    ap.add_argument("--rng", type=int, default=0, metavar="N",
                    help="spawn N RNG-only workers (GPU-less testing)")
    ap.add_argument("--ctx", type=int, default=4096,
                    help="context size for spawned workers")
    ap.add_argument("--kv-slots", type=int, default=None,
                    help="KV slot pool for spawned workers")
    ap.add_argument("--ngl", type=int, default=None,
                    help="GPU layers to offload (AUTOCOG_NGL) for spawned "
                         "workers; 99 = whole model. Without it the models "
                         "load CPU-ONLY — GPU campaigns must pass this")
    ap.add_argument("--sanity", action="store_true",
                    help="gate the campaign on per-worker sanity probes "
                         "(affinity echo, VRAM delta, GPU utilization, "
                         "one-question probe run)")
    ap.add_argument("--sanity-only", action="store_true",
                    help="run the sanity probes and exit")
    ap.add_argument("--ready-timeout", type=float, default=300,
                    help="seconds to wait for each worker's model load")
    ap.add_argument("--keep-workers", action="store_true",
                    help="leave spawned workers running afterwards")
    args = ap.parse_args(argv)

    manifest, base, out = load_campaign(args.manifest)
    os.makedirs(out, exist_ok=True)
    worker_logs = os.path.join(out, "workers")
    os.makedirs(worker_logs, exist_ok=True)

    def log(msg):
        print(msg, flush=True)

    models = run_models(manifest, base)
    workers = build_topology(args, models, worker_logs)

    failures = []
    try:
        if args.per_gpu > 1:
            # Shared-GPU spawns serialize (spawn -> ready -> next) so each
            # worker's VRAM delta attributes to its own model load.
            for w in workers:
                if w.spawned:
                    w.spawn()
                w.wait_ready(args.ready_timeout)
                log(f"[up] {w.describe()}")
        else:
            for w in workers:
                if w.spawned:
                    w.spawn()
            for w in workers:
                w.wait_ready(args.ready_timeout)
                log(f"[up] {w.describe()}")
        if args.sanity or args.sanity_only:
            sanity_failures = sanity_check(workers, out, log)
            if sanity_failures:
                for f in sanity_failures:
                    log(f"[sanity] FAIL {f}")
                return 2
            log("[sanity] all workers pass")
            if args.sanity_only:
                return 0
        failures = dispatch(args.manifest, manifest, base, out, workers, log)
    finally:
        if args.keep_workers:
            log("[keep-workers] " + ", ".join(w.url for w in workers if w.spawned))
        else:
            for w in workers:
                if w.spawned:
                    w.stop()

    if failures:
        log(f"[failed] {len(failures)} run(s): " + ", ".join(failures))
    log(f"campaign complete: {out}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
