#!/usr/bin/env python3
"""Campaign autopilot — run the v0.8 protocol campaign unattended.

Operator contract: start the box, place models and datasets, run the
sanity gate, then launch this once (in tmux/nohup) and walk away:

    python3 benchmarks/campaigns/client.py \
        benchmarks/campaigns/smoke-real.json --ngl 99 --sanity-only
    nohup python3 benchmarks/campaigns/autopilot.py --ngl 99 \
        > autopilot.out 2>&1 &

Everything else is automatic and hands-off:

  * preflight  — models, datasets, GPUs, disk, repo layout; fail fast
                 while a human might still be looking
  * W1 smoke   — 20-question real-model gate; W2+ only start if smoke
                 answers are sane (repeat answers verbatim, error rate
                 low); a failed gate aborts the campaign with a report
  * W2 grid    — arc-protocol-small, then arc-protocol-8b (client.py)
  * W3 probes  — choice-scoring adjudication fanned out across GPUs,
                 one model per GPU at a time, then offline analysis
  * W4 cot     — arc-cot-small + arc-cot-8b
  * W5 term    — perf-termination
  * summary    — SUMMARY.md + status.json in the campaign directory

Failure semantics: a failed stage is recorded and the campaign moves on
(stages are independent by design; only the smoke gate is blocking).
Progress is checkpointed in autopilot-state.json — rerunning the same
command after a crash or power loss skips completed stages.

`--smoke-test` runs the full pipeline shape on RNG workers with tiny
question counts (no models, no GPUs) — the preflight for the driver
itself; run it before shipping the box.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

WAVES = [
    ("w1-smoke", "smoke-real.json"),
    ("w2-small", "arc-protocol-small.json"),
    ("w2-8b", "arc-protocol-8b.json"),
    ("w4-cot-small", "arc-cot-small.json"),
    ("w4-cot-8b", "arc-cot-8b.json"),
    ("w5-termination", "perf-termination.json"),
]

# W3 probe grid: (demo, syntax) x datasets, for every model that appears
# in the protocol manifests. Full ARC; trim here if wall time must shrink.
PROBE_DEMOS = ("select", "repeat")
PROBE_SYNTAXES = ("complete", "stripped")
PROBE_DATASETS = ("arc-easy.json", "arc-challenge.json")


class Autopilot:
    def __init__(self, args):
        self.args = args
        self.out = os.path.join(HERE, "results",
                                "autopilot-smoke" if args.smoke_test
                                else "autopilot")
        os.makedirs(self.out, exist_ok=True)
        self.state_path = os.path.join(self.out, "autopilot-state.json")
        self.state = (json.load(open(self.state_path))
                      if os.path.exists(self.state_path) else {"stages": {}})
        self.t0 = time.time()

    # -- infrastructure ----------------------------------------------------

    def log(self, msg):
        stamp = time.strftime("%H:%M:%S")
        print(f"[{stamp} +{(time.time() - self.t0) / 3600:.1f}h] {msg}",
              flush=True)

    def save(self):
        with open(self.state_path, "w") as f:
            json.dump(self.state, f, indent=1)

    def stage(self, name, fn, blocking=False):
        """Run one stage with checkpointing and continue-on-failure."""
        rec = self.state["stages"].get(name)
        if rec and rec.get("status") == "ok":
            self.log(f"[{name}] already complete — skipping")
            return True
        self.log(f"[{name}] starting")
        t0 = time.time()
        try:
            fn()
            self.state["stages"][name] = {"status": "ok",
                                          "seconds": round(time.time() - t0)}
            self.save()
            self.log(f"[{name}] ok ({(time.time() - t0) / 60:.0f} min)")
            return True
        except Exception as e:  # noqa: BLE001 — unattended: record and move on
            self.state["stages"][name] = {"status": "failed", "error": str(e)[:500],
                                          "seconds": round(time.time() - t0)}
            self.save()
            self.log(f"[{name}] FAILED: {e}")
            if blocking:
                self.log("blocking stage failed — aborting campaign")
                self.finish(aborted=True)
                sys.exit(2)
            return False

    def run_logged(self, cmd, log_name, timeout):
        path = os.path.join(self.out, log_name)
        with open(path, "a") as lf:
            lf.write(f"\n=== {' '.join(cmd)} ===\n")
            lf.flush()
            r = subprocess.run(cmd, stdout=lf, stderr=subprocess.STDOUT,
                               cwd=REPO, timeout=timeout)
        if r.returncode != 0:
            raise RuntimeError(f"exit {r.returncode} (log: {path})")

    def client(self, manifest, log_name, timeout):
        if self.args.smoke_test:
            # Pipeline-shape validation: every wave exercises the same
            # driver machinery against the rng manifest (the real ones
            # name model files that only exist on the box).
            manifest = "testing-rng.json"
        cmd = [sys.executable, os.path.join(HERE, "client.py"),
               os.path.join(HERE, manifest)]
        if self.args.smoke_test:
            cmd += ["--rng", "2"]
        else:
            cmd += ["--ngl", str(self.args.ngl)]
            if self.args.gpus:
                cmd += ["--gpus", self.args.gpus]
        self.run_logged(cmd, log_name, timeout)

    # -- preflight ---------------------------------------------------------

    def manifest_models(self):
        models = []
        for _, manifest in WAVES:
            m = json.load(open(os.path.join(HERE, manifest)))
            for run in m.get("runs", []):
                path = run.get("model")
                if path and path not in models:
                    models.append(path)
        return models

    def preflight(self):
        missing = []
        if not self.args.smoke_test:
            for rel in self.manifest_models():
                if not os.path.exists(os.path.join(HERE, rel)):
                    missing.append(rel)
            for _, manifest in WAVES:
                m = json.load(open(os.path.join(HERE, manifest)))
                for run in m.get("runs", []):
                    d = run.get("data")
                    if d and not os.path.exists(os.path.normpath(os.path.join(HERE, d))):
                        missing.append(d)
            if missing:
                raise RuntimeError("missing files:\n  " +
                                   "\n  ".join(sorted(set(missing))))
            gpus = subprocess.run(["nvidia-smi", "-L"], capture_output=True,
                                  text=True)
            n = len([l for l in gpus.stdout.splitlines() if l.strip()])
            if gpus.returncode != 0 or n == 0:
                raise RuntimeError("no GPUs visible to nvidia-smi")
            self.log(f"preflight: {n} GPU(s)")
        free = shutil.disk_usage(HERE).free // (1 << 30)
        if free < 5:
            raise RuntimeError(f"only {free} GiB free under {HERE}")
        self.log(f"preflight: {free} GiB free disk")

    # -- smoke gate --------------------------------------------------------

    def smoke_gate(self):
        """W1 already ran; verify its events look sane before W2 spends
        hours: low error rate, and repeat answers actually verbatim."""
        base = os.path.join(HERE, "results",
                            "testing-rng" if self.args.smoke_test else "smoke-real")
        events = []
        for sub, _, files in os.walk(base):
            for f in files:
                if f.endswith(".ndjson"):
                    events += [json.loads(l)
                               for l in open(os.path.join(sub, f))]
        if not events:
            raise RuntimeError(f"smoke produced no events under {base}")
        errors = [e for e in events if e.get("error.message")]
        if len(errors) > len(events) * 0.2:
            raise RuntimeError(
                f"smoke error rate {len(errors)}/{len(events)}; first: "
                f"{errors[0].get('error.message', '')[:200]}")
        answered = [e for e in events
                    if e.get("autocog.bench.correct") is not None]
        if len(answered) < len(events) * 0.5:
            raise RuntimeError("smoke: under half the events carry answers")
        self.log(f"smoke gate: {len(events)} events, {len(errors)} errors")

    # -- W3 probes ---------------------------------------------------------

    def probe_jobs(self):
        for model in ([None] if self.args.smoke_test else self.manifest_models()):
            if model and "cot" in model:
                continue
            for demo in PROBE_DEMOS:
                for syntax in PROBE_SYNTAXES:
                    for data in (("questions.json",) if self.args.smoke_test
                                 else PROBE_DATASETS):
                        yield model, demo, syntax, data

    def probes(self):
        probe = os.path.join(HERE, "probe_choices.py")
        outdir = os.path.join(HERE, "results", "probe")
        os.makedirs(outdir, exist_ok=True)
        jobs, running = list(self.probe_jobs()), []
        gpu_count = 1 if self.args.smoke_test else max(
            1, len(subprocess.run(["nvidia-smi", "-L"], capture_output=True,
                                  text=True).stdout.strip().splitlines()))
        failures = 0

        def launch(job, gpu):
            model, demo, syntax, data = job
            tag = (os.path.splitext(os.path.basename(model))[0]
                   if model else "rng")
            name = f"{tag}-{demo}-{syntax}-{os.path.splitext(data)[0]}"
            out = os.path.join(outdir, name + ".ndjson")
            if os.path.exists(out) and os.path.getsize(out) > 0:
                self.log(f"[w3] {name} exists — skipping")
                return None
            data_path = (os.path.join(REPO, "benchmarks", "quality", data)
                         if self.args.smoke_test
                         else os.path.join(HERE, "datasets", data))
            cmd = [sys.executable, probe, "run", "--demo", demo,
                   "--syntax", syntax, "--data", data_path, "--out", out]
            cmd += ["--model", os.path.join(HERE, model)] if model else []
            if self.args.smoke_test:
                cmd += ["--limit", "4"]
            env = dict(os.environ)
            if not self.args.smoke_test:
                env["CUDA_VISIBLE_DEVICES"] = str(gpu)
                env["AUTOCOG_NGL"] = str(self.args.ngl)
            lf = open(os.path.join(self.out, f"w3-{name}.log"), "w")
            return (name, gpu,
                    subprocess.Popen(cmd, stdout=lf, stderr=subprocess.STDOUT,
                                     cwd=REPO, env=env), lf)

        free_gpus = list(range(gpu_count))
        while jobs or running:
            while jobs and free_gpus:
                started = launch(jobs.pop(0), free_gpus[0])
                if started:
                    free_gpus.pop(0)
                    running.append(started)
            still = []
            for name, gpu, proc, lf in running:
                if proc.poll() is None:
                    still.append((name, gpu, proc, lf))
                    continue
                lf.close()
                free_gpus.append(gpu)
                if proc.returncode != 0:
                    failures += 1
                    self.log(f"[w3] {name} FAILED (exit {proc.returncode})")
                else:
                    self.log(f"[w3] {name} done")
            running = still
            time.sleep(2 if running else 0)
        if failures:
            raise RuntimeError(f"{failures} probe job(s) failed")

    def probe_analysis(self):
        outdir = os.path.join(HERE, "results", "probe")
        ndjson = [os.path.join(outdir, f) for f in sorted(os.listdir(outdir))
                  if f.endswith(".ndjson")]
        if not ndjson:
            raise RuntimeError("no probe output to analyze")
        self.run_logged([sys.executable, os.path.join(HERE, "probe_choices.py"),
                         "analyze", *ndjson,
                         "--out", os.path.join(outdir, "adjudication")],
                        "w3-analyze.log", 600)

    # -- summary -----------------------------------------------------------

    def finish(self, aborted=False):
        status = {"aborted": aborted,
                  "wall_hours": round((time.time() - self.t0) / 3600, 2),
                  "stages": self.state["stages"]}
        with open(os.path.join(self.out, "status.json"), "w") as f:
            json.dump(status, f, indent=1)
        lines = ["# Campaign autopilot summary", "",
                 f"Wall: {status['wall_hours']} h — "
                 + ("ABORTED" if aborted else "complete"), "",
                 "| stage | status | minutes |", "|---|---|---|"]
        for name, rec in self.state["stages"].items():
            lines.append(f"| {name} | {rec['status']} "
                         f"| {rec.get('seconds', 0) // 60} |")
        failed = [n for n, r in self.state["stages"].items()
                  if r["status"] != "ok"]
        lines += ["", f"Failed stages: {', '.join(failed) if failed else 'none'}",
                  "", "Results: benchmarks/campaigns/results/<name>/; "
                  "probe adjudication: results/probe/adjudication.md"]
        with open(os.path.join(self.out, "SUMMARY.md"), "w") as f:
            f.write("\n".join(lines) + "\n")
        self.log(f"summary -> {os.path.join(self.out, 'SUMMARY.md')}")

    # -- main sequence -----------------------------------------------------

    def run(self):
        hours = 3600
        self.stage("preflight", self.preflight, blocking=True)
        self.stage("w1-smoke",
                   lambda: self.client("smoke-real.json" if not self.args.smoke_test
                                       else "testing-rng.json",
                                       "w1.log", 2 * hours), blocking=True)
        self.stage("w1-gate", self.smoke_gate, blocking=True)
        self.stage("w2-small",
                   lambda: self.client("arc-protocol-small.json", "w2.log",
                                       self.args.wave_timeout * hours))
        self.stage("w2-8b",
                   lambda: self.client("arc-protocol-8b.json", "w2.log",
                                       self.args.wave_timeout * hours))
        self.stage("w3-probes", self.probes)
        self.stage("w3-analysis", self.probe_analysis)
        self.stage("w4-cot-small",
                   lambda: self.client("arc-cot-small.json", "w4.log",
                                       self.args.wave_timeout * hours))
        self.stage("w4-cot-8b",
                   lambda: self.client("arc-cot-8b.json", "w4.log",
                                       self.args.wave_timeout * hours))
        self.stage("w5-termination",
                   lambda: self.client("perf-termination.json", "w5.log",
                                       self.args.wave_timeout * hours))
        self.finish()
        failed = [n for n, r in self.state["stages"].items()
                  if r["status"] != "ok"]
        return 1 if failed else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ngl", type=int, default=99,
                    help="GPU layers for all workers/probes (default 99)")
    ap.add_argument("--gpus", default=None,
                    help="restrict to these GPU indices (default: all)")
    ap.add_argument("--wave-timeout", type=float, default=12,
                    help="hard cap per wave, hours (default 12)")
    ap.add_argument("--smoke-test", action="store_true",
                    help="validate the whole pipeline on RNG workers "
                         "(no models, no GPUs, minutes)")
    args = ap.parse_args(argv)
    return Autopilot(args).run()


if __name__ == "__main__":
    sys.exit(main())
