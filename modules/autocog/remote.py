"""
RemoteEngine — dispatches evaluate_prompt over HTTP to a remote server.

Drop-in replacement for Engine. Works with Context unchanged.

Level 2 (RPC server):
    engine = RemoteEngine("http://gpu-box:8080")
    ctx = Context(program, engine, "init_idea", inputs, externals)

Level 1 (serve server):
    result = remote_run("http://gpu-box:8080", entry="main", **inputs)
"""

import json
import time
import urllib.request
import urllib.error


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
                raise RuntimeError(f"Remote evaluation failed: {status['error']}")
        raise TimeoutError(
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
            raise RuntimeError(
                f"Program did not complete after {max_steps} steps "
                f"(at prompt '{ctx.prompt}')"
            )
        return ctx.result


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
            raise RuntimeError(f"Remote execution failed: {status['error']}")
    raise TimeoutError(f"Remote execution timed out after {timeout}s")
