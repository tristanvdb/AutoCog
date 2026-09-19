"""The campaign's own event stream.

One NDJSON file per results root, appended by the app and by the bench alike,
which is what the monitor tails. The app's lines describe the campaign
(planned, started, a run beginning or ending); the bench's describe samples.
"""

import datetime
import json
import os
import threading


def attach_bench_stream(path):
    """Route the library's bench events into the campaign's stream.

    `autocog.bench` logs every sample through the `autocog.bench.stream`
    logger; with an ECS formatter on it, each becomes one NDJSON line. That
    is what makes the monitor live rather than a post-mortem: without this
    the file holds only the app's own lifecycle lines and a running campaign
    looks like nothing is happening.
    """
    import logging

    from autocog._json_formatter import ECSFormatter

    log = logging.getLogger("autocog.bench.stream")
    for existing in list(log.handlers):
        log.removeHandler(existing)
    handler = logging.FileHandler(path)
    handler.setFormatter(ECSFormatter())
    log.addHandler(handler)
    log.setLevel(logging.INFO)
    log.propagate = False
    return log


class Events:
    def __init__(self, path):
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        self.path = path
        self._lock = threading.Lock()

    def emit(self, action, campaign, **fields):
        ts = datetime.datetime.now(datetime.timezone.utc)
        ev = {
            "@timestamp": ts.strftime("%Y-%m-%dT%H:%M:%S.")
                          + f"{ts.microsecond // 1000:03d}Z",
            "event.action": action,
            "autocog.campaign.name": campaign,
        }
        ev.update(fields)
        with self._lock, open(self.path, "a") as f:
            f.write(json.dumps({"autocog.bench": ev}) + "\n")
        return ev
