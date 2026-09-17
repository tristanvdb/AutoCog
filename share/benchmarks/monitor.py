#!/usr/bin/env python3
"""Campaign status screen — tail the launcher's event stream and render
live progress: campaigns done/running/remaining, phase and run progress,
observed rates with an ETA, accuracy sliced along the campaign's
declared axes, and per-worker throughput/occupancy polled from the
workers' status API (GET /stats: FTA rates over 1m/5m/30m, occupancy,
queue depth — workers advertised by the launcher's campaign.workers
event, polled only while the campaign runs).

    monitor.py [EVENTS]          # default: $AUTOCOG_WORKDIR|./results/campaign-events.ndjson
    monitor.py --once [EVENTS]   # one snapshot, no screen refresh

The stream is NDJSON: ECS lines whose `autocog.bench` field carries a
bench event (per-question quality events, per-cell perf events, and the
executor's run.start/end/skip lifecycle), plus the launcher's own
campaign.plan / campaign.start / campaign.end lines. Reading is
incremental; the monitor can attach mid-run or replay a finished stream.
"""

import argparse
import json
import os
import sys
import time
import urllib.request


def default_events_path():
    wd = os.environ.get("AUTOCOG_WORKDIR") or os.getcwd()
    return os.path.join(wd, "results", "campaign-events.ndjson")


def fmt_dt(seconds):
    if seconds is None:
        return "--:--"
    seconds = int(seconds)
    h, m, s = seconds // 3600, (seconds % 3600) // 60, seconds % 60
    return f"{h}:{m:02d}:{s:02d}" if h else f"{m}:{s:02d}"


def bar(done, total, width=30):
    if not total:
        return "-" * width
    fill = int(width * min(done, total) / total)
    return "#" * fill + "-" * (width - fill)


class State:
    def __init__(self):
        self.campaigns = {}      # name -> {"phases": {name: total}, "axes": [...],
                                 #          "status", "started", "ended"}
        self.order = []
        self.runs = {}           # (campaign, phase, label) -> {"status", "t0", "t1", "kind"}
        self.q_events = []       # (ts, campaign?, axes-values dict, correct, wall)
        self.cells = 0
        self.running = None       # campaign currently live in the stream
        self.last_error = None
        self.last_event_ts = None

    def campaign(self, name):
        if name not in self.campaigns:
            self.campaigns[name] = {"phases": {}, "axes": ["model", "syntax", "program"],
                                    "status": "pending", "started": None,
                                    "ended": None, "workers": []}
            self.order.append(name)
        return self.campaigns[name]

    def feed(self, obj):
        ev = obj.get("autocog.bench") or obj      # nested (executor) or flat (launcher)
        action = ev.get("event.action", "")
        ts = obj.get("@timestamp") or ev.get("@timestamp")
        self.last_event_ts = ts or self.last_event_ts

        if action == "campaign.plan":
            c = self.campaign(ev["campaign"])
            c["phases"] = {p["name"]: p["runs"] for p in ev.get("phases", [])}
            if ev.get("axes"):
                c["axes"] = ev["axes"]
        elif action == "campaign.start":
            c = self.campaign(ev["campaign"])
            c["status"], c["started"] = "running", ts
            self.running = ev["campaign"]
        elif action == "campaign.workers":
            self.campaign(ev["campaign"])["workers"] = ev.get("workers", [])
        elif action == "campaign.end":
            c = self.campaign(ev["campaign"])
            c["status"], c["ended"] = "done", ts
            if self.running == ev["campaign"]:
                self.running = None
        elif action in ("run.start", "run.end", "run.skip"):
            key = (ev.get("autocog.campaign.name"), ev.get("autocog.campaign.phase"),
                   ev.get("autocog.campaign.run"))
            rec = self.runs.setdefault(key, {"status": "running", "t0": ts, "t1": None,
                                             "kind": ev.get("autocog.campaign.kind")})
            if action == "run.start":
                rec.update(status="running", t0=ts)
            elif action == "run.skip":
                rec.update(status="skipped", t1=ts)
            else:
                rec.update(status="ok" if ev.get("autocog.campaign.ok") else "failed",
                           t1=ts)
                if not ev.get("autocog.campaign.ok") and ev.get("error.message"):
                    self.last_error = ev["error.message"]
        elif action == "quality.run":
            # Bench events carry no campaign field: attribute them to the
            # campaign running when they arrived. One events file spans
            # every campaign of a session, so unscoped aggregates would mix
            # a finished campaign's accuracy into a running one's.
            ev["_campaign"] = self.running or self.current()
            ev["_ts"] = parse_ts(ts) or parse_ts(ev.get("@timestamp"))
            self.q_events.append(ev)
            if ev.get("error.message"):
                self.last_error = ev["error.message"]
        elif action == "eval.summary":
            self.cells += 1

    # -- derived ----------------------------------------------------------

    def current(self):
        """The running campaign, else the last one seen (a finished
        stream still renders its detail pane)."""
        for name in self.order:
            if self.campaigns[name]["status"] == "running":
                return name
        return self.order[-1] if self.order else None

    def run_counts(self, name):
        done = running = failed = 0
        for (c, _, _), rec in self.runs.items():
            if c != name:
                continue
            if rec["status"] in ("ok", "skipped"):
                done += 1
            elif rec["status"] == "failed":
                done += 1
                failed += 1
            else:
                running += 1
        total = sum(self.campaigns[name]["phases"].values()) or None
        return done, running, failed, total

    def rates(self, name=None):
        """AGGREGATE questions/min over the trailing window (wall-clock
        across all concurrent lanes, not per-lane busy time), and mean
        seconds/run — both for ONE campaign, since an events file spans a
        whole session and another campaign's runs say nothing about this
        one's pace."""
        qpm = None
        recent = [e for e in self.q_events
                  if name is None or e.get("_campaign") == name][-500:]
        stamps = [e["_ts"] for e in recent if e.get("_ts")]
        if len(stamps) >= 2:
            span = stamps[-1] - stamps[0]
            if span > 0:
                qpm = 60.0 * (len(stamps) - 1) / span
        durations = []
        for (c, _, _), rec in self.runs.items():
            if name is not None and c != name:
                continue
            if rec["status"] in ("ok", "failed") and rec["t0"] and rec["t1"]:
                d = parse_ts(rec["t1"]) - parse_ts(rec["t0"])
                if d > 0:
                    durations.append(d)
        spr = (sum(durations) / len(durations)) if durations else None
        return qpm, spr

    def slices(self, name):
        """Accuracy by axis. Beyond the declared axes, every label a program
        carries is an axis of its own: programs tagged `select` group together
        whatever they are named, and so do the ones tagged `record`. The
        labels are opaque here — a descriptor decides what is worth
        contrasting, and this just counts."""
        axes = self.campaigns[name]["axes"] if name in self.campaigns else []
        mine = [e for e in self.q_events if e.get("_campaign") == name]
        out = {}
        for axis in axes:
            key = f"autocog.bench.{axis}"
            agg = {}
            for e in mine:
                v, c = e.get(key), e.get("autocog.bench.correct")
                if v is None or c is None:
                    continue
                n, k = agg.get(v, (0, 0))
                agg[v] = (n + 1, k + (1 if c else 0))
            if agg:
                out[axis] = agg
        agg = {}
        for e in mine:
            c = e.get("autocog.bench.correct")
            if c is None:
                continue
            for label in e.get("autocog.bench.labels") or []:
                n, k = agg.get(label, (0, 0))
                agg[label] = (n + 1, k + (1 if c else 0))
        if agg:
            out["label"] = agg
        return out


def fetch_stats(url, timeout=2.0):
    """One worker's GET /stats. Returns the stats dict, or a reason string:
    "busy" when the request timed out (the worker is alive but its event
    loop is occupied) and "down" when the connection itself failed (not
    spawned yet, or torn down). Reporting a busy worker as unreachable is
    exactly backwards -- it is the most loaded workers that stall."""
    import socket

    if "://" not in url:
        url = "http://" + url
    try:
        with urllib.request.urlopen(f"{url}/stats", timeout=timeout) as resp:
            return json.loads(resp.read())
    except socket.timeout:
        return "busy"
    except urllib.error.URLError as e:
        return "busy" if isinstance(e.reason, socket.timeout) else "down"
    except OSError:
        return "down"


def render_workers(workers):
    """Per-worker throughput/occupancy lines from the status API."""
    lines = [" workers:"]
    for w in workers:
        label = f"{w['url']} [{','.join(w.get('models', []))}]"
        stats = fetch_stats(w["url"])
        if isinstance(stats, str):
            lines.append(f"   {label:<44} "
                         + ("busy (status endpoint stalled)" if stats == "busy"
                            else "down (no connection)"))
            continue
        win = stats.get("windows", {})

        def rate(k):
            return win.get(k, {}).get("rate")

        rates = "  ".join(
            f"{k} {rate(k):.2f}" if rate(k) is not None else f"{k} -"
            for k in ("1m", "5m", "30m"))
        occ = win.get("1m", {}).get("occupancy")
        lines.append(
            f"   {label:<44} fta/s {rates}   "
            f"occ {occ:4.0%}  q {stats.get('pending', 0)}"
            if occ is not None else
            f"   {label:<44} fta/s {rates}   q {stats.get('pending', 0)}")
    return lines


def parse_ts(ts):
    """Stream timestamps come in two shapes: the ECS formatter writes
    '...123Z', bench events carry Python isoformat '...123456+00:00'.
    Parse both — treating one as unparseable silently zeroed every rate
    computed from quality events."""
    from datetime import datetime, timezone

    if not isinstance(ts, str):
        return 0.0
    try:
        return datetime.strptime(ts, "%Y-%m-%dT%H:%M:%S.%fZ").replace(
            tzinfo=timezone.utc).timestamp()
    except ValueError:
        pass
    try:
        parsed = datetime.fromisoformat(ts.replace("Z", "+00:00"))
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=timezone.utc)
        return parsed.timestamp()
    except ValueError:
        return 0.0


def render(state):
    lines = ["=== campaign monitor ===", ""]
    for name in state.order:
        c = state.campaigns[name]
        done, running, failed, total = state.run_counts(name)
        mark = {"pending": " ", "running": ">", "done": "*"}[c["status"]]
        prog = f"{done}/{total if total else '?'}"
        extra = f"  FAILED:{failed}" if failed else ""
        lines.append(f" [{mark}] {name:<24} {c['status']:<8} runs {prog}{extra}")
    cur = state.current()
    if cur:
        done, running, failed, total = state.run_counts(cur)
        qpm, spr = state.rates(cur)
        lines += ["", f" current: {cur}",
                  f"   [{bar(done, total)}] {done}/{total if total else '?'} runs"
                  + (f"  ({running} in flight)" if running else "")]
        if total and spr:
            lines.append(f"   rate: {qpm:.1f} q/min" if qpm else "   rate: --"
                         )
            # Remaining runs spread over the lanes currently in flight.
            eta = (total - done) * spr / max(1, running)
            lines.append(f"   ~{fmt_dt(spr)}/run, ETA {fmt_dt(eta)}")
        elif qpm:
            lines.append(f"   rate: {qpm:.1f} q/min")
        active = [f"{k[1]}/{k[2]}" for k, r in state.runs.items()
                  if k[0] == cur and r["status"] == "running"]
        if active:
            lines.append("   running: " + ", ".join(active[:4]))
        if state.campaigns[cur]["status"] == "running" \
                and state.campaigns[cur]["workers"]:
            lines += [""] + render_workers(state.campaigns[cur]["workers"])
        for axis, agg in state.slices(cur).items():
            lines.append(f"   accuracy by {axis}:")
            for v, (n, k) in sorted(agg.items(), key=lambda x: -x[1][0])[:8]:
                lines.append(f"     {str(v):<28} {k / n:6.1%}  (n={n})")
    if state.cells:
        lines += ["", f" perf cells completed: {state.cells}"]
    if state.last_error:
        lines += ["", f" last error: {state.last_error[:100]}"]
    lines += ["", f" last event: {state.last_event_ts or '-'}"]
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("events", nargs="?", default=default_events_path())
    ap.add_argument("--once", action="store_true", help="print one snapshot")
    ap.add_argument("--interval", type=float, default=2.0)
    args = ap.parse_args(argv)

    state = State()
    offset = 0
    while True:
        if os.path.exists(args.events):
            size = os.path.getsize(args.events)
            if size < offset:     # stream truncated/rotated: start over
                state, offset = State(), 0
            if size > offset:
                with open(args.events) as f:
                    f.seek(offset)
                    for line in f:
                        try:
                            state.feed(json.loads(line))
                        except (json.JSONDecodeError, KeyError):
                            continue
                    offset = f.tell()
        out = render(state)
        if args.once:
            print(out)
            return 0
        sys.stdout.write("\x1b[2J\x1b[H" + out + "\n")
        sys.stdout.flush()
        time.sleep(args.interval)


if __name__ == "__main__":
    sys.exit(main())
