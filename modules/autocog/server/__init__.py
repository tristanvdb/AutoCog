"""
Server infrastructure — async request queue and status tracking.

All three server levels share this pattern:
  POST /submit → {request_id}
  GET /status/{request_id} → {state, result?, error?}
  GET /stats → throughput/occupancy over trailing windows (status API)
"""

import asyncio
import time
import uuid
from collections import deque
from enum import Enum
from typing import Any, Callable, Dict, Optional

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel

from autocog.errors import OrchestrationError


def _sanitize(obj):
    """Make a result JSON-serializable (replace control characters in strings)."""
    if isinstance(obj, str):
        # Replace control characters (except \n, \r, \t) with spaces
        return "".join(c if c >= " " or c in "\n\r\t" else " " for c in obj)
    elif isinstance(obj, dict):
        return {k: _sanitize(v) for k, v in obj.items()}
    elif isinstance(obj, list):
        return [_sanitize(v) for v in obj]
    return obj


class RequestState(str, Enum):
    pending = "pending"
    running = "running"
    complete = "complete"
    error = "error"


class RequestStatus(BaseModel):
    request_id: str
    state: RequestState
    result: Optional[Any] = None
    error: Optional[str] = None


class RequestQueue:
    """Async request queue with background worker."""

    #: trailing windows reported by stats(): label -> seconds
    STAT_WINDOWS = (("1m", 60), ("5m", 300), ("30m", 1800))

    def __init__(self, max_workers: int = 1):
        self._requests: Dict[str, dict] = {}
        self._queue: asyncio.Queue = asyncio.Queue()
        self._max_workers = max_workers
        self._workers = []
        # Job accounting (all mutation happens on the event loop):
        self._t0 = time.time()
        self._done: deque = deque()   # (end_time, duration, ok)
        self._running: Dict[str, float] = {}   # request_id -> start time
        self._total = 0
        self._errors = 0

    def submit(self, func: Callable, **kwargs) -> str:
        """Submit a request. Returns request_id."""
        request_id = str(uuid.uuid4())
        self._requests[request_id] = {
            "state": RequestState.pending,
            "result": None,
            "error": None,
        }
        self._queue.put_nowait((request_id, func, kwargs))
        return request_id

    def status(self, request_id: str) -> RequestStatus:
        """Get the status of a request."""
        if request_id not in self._requests:
            raise OrchestrationError(f"Unknown request: {request_id}")
        r = self._requests[request_id]
        return RequestStatus(
            request_id=request_id,
            state=r["state"],
            result=r["result"],
            error=r["error"],
        )

    async def _worker(self):
        """Background worker — processes requests sequentially."""
        while True:
            request_id, func, kwargs = await self._queue.get()
            self._requests[request_id]["state"] = RequestState.running
            self._running[request_id] = time.time()
            ok = True
            try:
                # Run CPU-bound work in a thread to not block the event loop
                loop = asyncio.get_event_loop()
                result = await loop.run_in_executor(None, lambda: func(**kwargs))
                # Ensure result is JSON-serializable
                result = _sanitize(result)
                self._requests[request_id]["state"] = RequestState.complete
                self._requests[request_id]["result"] = result
            except Exception as e:
                ok = False
                self._requests[request_id]["state"] = RequestState.error
                self._requests[request_id]["error"] = str(e)
            finally:
                start = self._running.pop(request_id, None)
                now = time.time()
                self._done.append((now, now - (start or now), ok))
                self._total += 1
                if not ok:
                    self._errors += 1
                self._prune(now)
                self._queue.task_done()

    def _prune(self, now):
        horizon = max(w for _, w in self.STAT_WINDOWS)
        while self._done and self._done[0][0] < now - horizon:
            self._done.popleft()

    def stats(self) -> dict:
        """Throughput and occupancy over the trailing STAT_WINDOWS.

        rate = completed jobs / window-span (span clipped to uptime);
        occupancy = busy-seconds / (span x lanes), in-flight jobs counted
        for their elapsed part, capped at 1.0."""
        now = time.time()
        self._prune(now)
        out = {
            "lanes": self._max_workers,
            "pending": self._queue.qsize(),
            "active": len(self._running),
            "total": self._total,
            "errors": self._errors,
            "uptime_seconds": round(now - self._t0, 1),
            "windows": {},
        }
        for label, w in self.STAT_WINDOWS:
            span = max(min(w, now - self._t0), 1e-9)
            edge = now - w
            jobs = [(t, d) for (t, d, _) in self._done if t >= edge]
            busy = sum(min(d, t - edge) for t, d in jobs)
            busy += sum(now - max(start, edge)
                        for start in self._running.values())
            out["windows"][label] = {
                "jobs": len(jobs),
                "rate": round(len(jobs) / span, 3),
                "occupancy": round(
                    min(busy / (span * self._max_workers), 1.0), 3),
            }
        return out

    async def start(self):
        """Start background workers."""
        for _ in range(self._max_workers):
            task = asyncio.create_task(self._worker())
            self._workers.append(task)

    async def stop(self):
        """Stop background workers."""
        for task in self._workers:
            task.cancel()
        self._workers.clear()


def add_status_endpoint(app: FastAPI, queue: RequestQueue):
    """Add the shared status API: GET /status/{request_id} and GET /stats."""

    @app.get("/status/{request_id}")
    async def get_status(request_id: str) -> RequestStatus:
        try:
            return queue.status(request_id)
        except (KeyError, OrchestrationError):
            raise HTTPException(status_code=404, detail=f"Unknown request: {request_id}")

    @app.get("/stats")
    async def get_stats() -> dict:
        return queue.stats()
