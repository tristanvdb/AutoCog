"""
Server infrastructure — async request queue and status tracking.

All three server levels share this pattern:
  POST /submit → {request_id}
  GET /status/{request_id} → {state, result?, error?}
"""

import asyncio
import uuid
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

    def __init__(self, max_workers: int = 1):
        self._requests: Dict[str, dict] = {}
        self._queue: asyncio.Queue = asyncio.Queue()
        self._max_workers = max_workers
        self._workers = []

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
            try:
                # Run CPU-bound work in a thread to not block the event loop
                loop = asyncio.get_event_loop()
                result = await loop.run_in_executor(None, lambda: func(**kwargs))
                # Ensure result is JSON-serializable
                result = _sanitize(result)
                self._requests[request_id]["state"] = RequestState.complete
                self._requests[request_id]["result"] = result
            except Exception as e:
                self._requests[request_id]["state"] = RequestState.error
                self._requests[request_id]["error"] = str(e)
            finally:
                self._queue.task_done()

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
    """Add the shared GET /status/{request_id} endpoint."""

    @app.get("/status/{request_id}")
    async def get_status(request_id: str) -> RequestStatus:
        try:
            return queue.status(request_id)
        except (KeyError, OrchestrationError):
            raise HTTPException(status_code=404, detail=f"Unknown request: {request_id}")
