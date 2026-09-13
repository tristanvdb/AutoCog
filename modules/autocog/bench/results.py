"""Result emission: ECS-flavored NDJSON (incremental append) + markdown.

Same event shapes as share/benchmarks/compute/sweep.py and share/benchmarks/quality/
run.py so existing consumers (summarize/compare/plot, the SIEM pipeline)
read bench output unchanged.
"""

import datetime
import json
import os
import platform


def host_name():
    return platform.node().split(".")[0]


def base_event(logger, action, extra=None):
    ev = {
        "log.level": "info",
        "log.logger": logger,
        "event.kind": "metric",
        "event.action": action,
        "service.name": "autocog",
        "host.name": host_name(),
    }
    if extra:
        ev.update(extra)
    return ev


def timestamp():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


_stream_log = None


def stream_event(event):
    """Mirror a bench event through the `autocog.bench` logger. With --json
    (ECSFormatter) every event becomes one NDJSON line on the configured
    sink — the live feed a campaign monitor tails. Without --json the
    events stay silent (human logs keep their shape)."""
    global _stream_log
    if _stream_log is None:
        import logging

        _stream_log = logging.getLogger("autocog.bench.stream")
    _stream_log.info(event.get("event.action", "bench.event"),
                     extra={"autocog_bench": event})


class NdjsonWriter:
    """Appends one event per line as soon as it is recorded — an interrupted
    run keeps everything it measured."""

    def __init__(self, path):
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        self.path = path
        self._f = open(path, "a")
        self.events = []

    def emit(self, event):
        event.setdefault("@timestamp", timestamp())
        self.events.append(event)
        self._f.write(json.dumps(event) + "\n")
        self._f.flush()
        stream_event(event)

    def close(self):
        self._f.close()


PERF_COLS = ["cell", "eval s", "wall s", "tok restore", "tok eval",
             "decode calls", "decode s", "sample s", "score s", "other s",
             "kv forks", "complete s", "choose s"]


def perf_md(path, title, events, ngl):
    with open(path, "w") as f:
        f.write(f"# Compute benchmark — {title}\n\n")
        if events:
            f.write(f"NGL: {ngl}\n\n")
        f.write("| " + " | ".join(PERF_COLS) + " |\n")
        f.write("|" + "---|" * len(PERF_COLS) + "\n")
        for r in events:
            name = r.get("autocog.bench.label") or (
                f"b{r['autocog.bench.beams']} a{r['autocog.bench.ahead']} w{r['autocog.bench.width']}")
            other = r["autocog.perf.advance_seconds"] - sum(
                r.get(f"autocog.perf.{k}.seconds", 0) for k in ("decode", "sample", "score"))
            row = [name,
                   f"{r['autocog.perf.advance_seconds']:.2f}",
                   f"{r['autocog.bench.wall_seconds']:.2f}",
                   str(r["autocog.perf.tokens.restore"]),
                   str(r["autocog.perf.tokens.eval"]),
                   str(r.get("autocog.perf.decode.calls", "-")),
                   f"{r.get('autocog.perf.decode.seconds', 0):.2f}",
                   f"{r.get('autocog.perf.sample.seconds', 0):.2f}",
                   f"{r.get('autocog.perf.score.seconds', 0):.2f}",
                   f"{other:.2f}",
                   str(r.get("autocog.perf.kv.forks", "-")),
                   f"{r['autocog.perf.complete.seconds']:.2f}",
                   f"{r['autocog.perf.choose.seconds']:.2f}"]
            f.write("| " + " | ".join(row) + " |\n")


def quality_md(path, title, events):
    cells = {}
    for ev in events:
        key = (ev["autocog.bench.syntax"], ev["autocog.bench.demo"])
        cells.setdefault(key, []).append(ev)
    with open(path, "w") as f:
        f.write(f"# Quality benchmark — {title}\n\n")
        f.write("| syntax | demo | accuracy | tok value | tok structure "
                "| friction value | friction structure |\n")
        f.write("|---|---|---|---|---|---|---|\n")
        for (syntax, demo), evs in sorted(cells.items()):
            n = len(evs)
            acc = sum(1 for e in evs if e.get("autocog.bench.correct")) / n
            def mean(key):
                vals = [e.get(key) for e in evs if e.get(key) is not None]
                return sum(vals) / len(vals) if vals else None
            tv = mean("autocog.bench.tokens.value")
            ts = mean("autocog.bench.tokens.structure")
            fv = mean("autocog.bench.friction.value")
            fs = mean("autocog.bench.friction.structure")
            fmt = lambda v: f"{v:.2f}" if v is not None else "-"
            f.write(f"| {syntax} | {demo} | {acc:.0%} | {fmt(tv)} | {fmt(ts)} "
                    f"| {fmt(fv)} | {fmt(fs)} |\n")
