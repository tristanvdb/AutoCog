"""
RemoteEngine — dispatches evaluate_prompt over HTTP to a remote server.

Drop-in replacement for Engine. Works with Context unchanged.

Level 2 (RPC server):
    engine = RemoteEngine("http://gpu-box:8080")
    ctx = Context(program, engine, "init_idea", inputs, externals)

Level 3 (backend server):
    engine = RemoteBackend("http://gpu-box:8080", syntax=..., search=...)
    frame = engine.evaluate_prompt(program, prompt, content)

Level 1 (serve server):
    result = remote_run("http://gpu-box:8080", entry="main", **inputs)
"""

import json
import time
import urllib.request
import urllib.error

from .errors import RemoteError, Timeout


class RemoteEngine:
    """Engine that dispatches prompt evaluation to a remote RPC server (level 2)."""

    def __init__(self, server_url, poll_interval=0.5, timeout=300):
        """
        Args:
            server_url: base URL of the RPC server (e.g. "http://localhost:8080")
            poll_interval: seconds between status polls
            timeout: maximum seconds to wait for a result
        """
        self.server_url = server_url.rstrip("/")
        self.poll_interval = poll_interval
        self.timeout = timeout
        # RemoteEngine doesn't need syntax_id or model_id
        self.syntax_id = None
        self.model_id = None

    def evaluate_prompt(self, program, prompt_name, content):
        """Submit a prompt to the remote server and wait for the result."""
        # Submit
        req_data = json.dumps({"prompt": prompt_name, "content": content}).encode()
        req = urllib.request.Request(
            f"{self.server_url}/prompt",
            data=req_data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(req) as resp:
            submit_result = json.loads(resp.read())
        request_id = submit_result["request_id"]

        # Poll
        return self._poll(request_id)

    def _poll(self, request_id):
        """Poll until the request completes or times out."""
        deadline = time.time() + self.timeout
        while time.time() < deadline:
            time.sleep(self.poll_interval)
            url = f"{self.server_url}/status/{request_id}"
            with urllib.request.urlopen(url) as resp:
                status = json.loads(resp.read())
            state = status["state"]
            if state == "complete":
                return status["result"]
            elif state == "error":
                raise RemoteError(f"Remote evaluation failed: {status['error']}")
        raise Timeout(
            f"Remote evaluation timed out after {self.timeout}s "
            f"(request_id={request_id})"
        )

    def run(self, program, entry="main", externals=None, max_steps=100, **inputs):
        """Run a program using the remote engine for evaluation.

        Channel resolution happens locally. Only evaluate_prompt is remote.
        """
        from .context import Context

        prompt = program.entry_prompt(entry)
        ctx = Context(program, self, prompt, inputs, externals or {})
        steps = 0
        while not ctx.done and steps < max_steps:
            ctx.step()
            steps += 1
        if not ctx.done:
            raise RemoteError(
                f"Program did not complete after {max_steps} steps "
                f"(at prompt '{ctx.prompt}')"
            )
        return ctx.result


class RemoteBackend:
    """Engine that runs the FTA evaluation step on a remote backend (level 3).

    Drop-in Engine replacement: instantiation (STA -> FTA) and FTT -> frame
    happen locally (it holds the program, syntax, and search), while the model
    evaluation (FTA -> FTT) is dispatched to a remote backend server's
    /evaluate endpoint, which returns the FTT.
    """

    def __init__(self, server_url, syntax=None, search=None,
                 poll_interval=0.5, timeout=300):
        """
        Args:
            server_url: base URL of the backend server (level 3)
            syntax: path to a syntax config (loaded locally for instantiation)
            search: path to a search config (loaded locally for instantiation)
        """
        from autocog.runtime.sta import runtime_sta_cxx

        self._sta = runtime_sta_cxx
        self.server_url = server_url.rstrip("/")
        self.poll_interval = poll_interval
        self.timeout = timeout
        self.syntax_id = runtime_sta_cxx.load_syntax(syntax) if syntax else None
        self.search_id = (runtime_sta_cxx.load_search(search)
                          if search else None)
        self.model_id = None  # evaluation is remote

    def evaluate_prompt(self, program, prompt_name, content, record_kinds=None):
        """Instantiate locally, evaluate remotely, walk the returned FTT locally."""
        fta_id = self._sta.instantiate(
            program.id, prompt_name, content, self.syntax_id, self.search_id
        )
        artifacts = {}
        try:
            # FTA comes across as a dict; send it to the backend, which returns
            # the FTT (also a dict over the wire envelope).
            fta = self._sta.get_fta(fta_id)
            if record_kinds and "fta" in record_kinds:
                artifacts["fta"] = fta

            ftt = self._evaluate_remote(fta)

            # Land the received FTT in the local store, then walk it locally
            # (model-free; this client may run where no model is loaded).
            ftt_id = self._sta.read_ftt(ftt)
            try:
                frame = self._sta.walk_ftt_to_frame(
                    program.id, prompt_name, ftt_id, content
                )

                if record_kinds:
                    if "frame" in record_kinds:
                        artifacts["frame"] = frame
                    if "ftt" in record_kinds:
                        artifacts["ftt"] = ftt
            finally:
                self._sta.release_ftt(ftt_id)
        finally:
            self._sta.release_fta(fta_id)

        if record_kinds is not None:
            return frame, artifacts
        return frame

    def _evaluate_remote(self, fta):
        """POST an FTA to the backend's /evaluate and return the resulting FTT."""
        req_data = json.dumps({"fta": fta}).encode()
        req = urllib.request.Request(
            f"{self.server_url}/evaluate",
            data=req_data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(req) as resp:
            submit_result = json.loads(resp.read())
        return _poll(self.server_url, submit_result["request_id"],
                     self.poll_interval, self.timeout)

    def run(self, program, entry="main", externals=None, max_steps=100, **inputs):
        """Run a program, dispatching only the evaluation step to the backend."""
        from .context import Context

        prompt = program.entry_prompt(entry)
        ctx = Context(program, self, prompt, inputs, externals or {})
        steps = 0
        while not ctx.done and steps < max_steps:
            ctx.step()
            steps += 1
        if not ctx.done:
            raise RemoteError(
                f"Program did not complete after {max_steps} steps "
                f"(at prompt '{ctx.prompt}')"
            )
        return ctx.result


def _poll(server_url, request_id, poll_interval, timeout):
    """Poll a server's /status endpoint until the request completes."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        time.sleep(poll_interval)
        with urllib.request.urlopen(f"{server_url}/status/{request_id}") as resp:
            status = json.loads(resp.read())
        state = status["state"]
        if state == "complete":
            return status["result"]
        elif state == "error":
            raise RemoteError(f"Remote evaluation failed: {status['error']}")
    raise Timeout(
        f"Remote evaluation timed out after {timeout}s (request_id={request_id})"
    )


def remote_run(server_url, entry="main", poll_interval=0.5, timeout=300, **inputs):
    """Level 1 convenience — run against a serve endpoint (no local program needed).

    Args:
        server_url: base URL of the serve server
        entry: entry point name
        **inputs: input values

    Returns:
        result from the program
    """
    url = server_url.rstrip("/")
    req_data = json.dumps({"entry": entry, "inputs": inputs}).encode()
    req = urllib.request.Request(
        f"{url}/run",
        data=req_data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req) as resp:
        submit_result = json.loads(resp.read())
    request_id = submit_result["request_id"]

    # Poll
    deadline = time.time() + timeout
    while time.time() < deadline:
        time.sleep(poll_interval)
        with urllib.request.urlopen(f"{url}/status/{request_id}") as resp:
            status = json.loads(resp.read())
        if status["state"] == "complete":
            return status["result"]
        elif status["state"] == "error":
            raise RemoteError(f"Remote execution failed: {status['error']}")
    raise Timeout(f"Remote execution timed out after {timeout}s")
