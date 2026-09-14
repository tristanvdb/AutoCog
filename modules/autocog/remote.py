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

    def evaluate_prompt(self, program, prompt_name, content, record_kinds=None):
        """Submit a prompt to the remote server and wait for the result.

        `record_kinds` exists for signature parity with Engine, but recording is
        not supported over a RemoteEngine: the RPC server returns only the prompt
        result, not the intermediate artifacts a Recorder needs. Requesting it
        raises NotImplementedError rather than silently dropping the recording.
        """
        if record_kinds:
            raise NotImplementedError(
                "recording is not supported over RemoteEngine; the RPC protocol "
                "returns only results, not the record artifacts a Recorder needs"
            )
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

    async def evaluate_prompt_async(self, program, prompt_name, content,
                                    record_kinds=None):
        """Async hook for Context.step_async (blocking transport for now)."""
        return self.evaluate_prompt(program, prompt_name, content,
                                    record_kinds=record_kinds)

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
        """Run a program using the remote engine for evaluation (sync facade).

        Channel resolution happens locally. Only evaluate_prompt is remote.
        """
        import asyncio

        return asyncio.run(self.run_async(
            program, entry=entry, externals=externals, max_steps=max_steps,
            **inputs))

    async def run_async(self, program, entry="main", externals=None,
                        max_steps=100, **inputs):
        from .context import Context

        prompt = program.entry_prompt(entry)
        ctx = Context(program, self, prompt, inputs, externals or {})
        steps = 0
        while not ctx.done and steps < max_steps:
            await ctx.step_async()
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
        #: model tag on the worker (None = the worker's default model)
        self.model_tag = None
        #: autocog.perf.* deltas of the most recent remote evaluation, as
        #: reported by the worker (worker clock), or None. Mirrors Engine.
        self.last_perf = None

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

            reply = self._evaluate_remote(fta)
            ftt = reply["ftt"]
            self.last_perf = reply.get("perf")
            if record_kinds and "perf" in record_kinds:
                artifacts["perf"] = self.last_perf

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

    async def evaluate_prompt_async(self, program, prompt_name, content,
                                    record_kinds=None):
        """evaluate_prompt with an awaitable transport: the loop stays free
        while this job sits in the worker's queue (store operations are
        local and fast; only the submit/poll waits are async)."""
        fta_id = self._sta.instantiate(
            program.id, prompt_name, content, self.syntax_id, self.search_id
        )
        artifacts = {}
        try:
            fta = self._sta.get_fta(fta_id)
            if record_kinds and "fta" in record_kinds:
                artifacts["fta"] = fta

            reply = await self._submit_async(
                "/evaluate", {"fta": fta, "model": self.model_tag})
            ftt = reply["ftt"]
            self.last_perf = reply.get("perf")
            if record_kinds and "perf" in record_kinds:
                artifacts["perf"] = self.last_perf

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

    async def score_frame_async(self, program, prompt_name, frame, content=None):
        """score_frame with an awaitable transport."""
        content = content or {}
        fta_id = self._sta.instantiate(
            program.id, prompt_name, content, self.syntax_id, self.search_id
        )
        try:
            ftt_id = self._sta.encode_frame(
                program.id, prompt_name, fta_id, frame, content
            )
            try:
                ftt = self._sta.get_ftt(ftt_id)
            finally:
                self._sta.release_ftt(ftt_id)
        finally:
            self._sta.release_fta(fta_id)
        reply = await self._submit_async(
            "/score", {"ftt": ftt, "model": self.model_tag})
        return reply["ftt"]

    async def _submit_async(self, endpoint, payload):
        """POST a job to a queued endpoint and poll asynchronously. The HTTP
        round trips themselves are short (submit returns a request id, status
        returns state) and run on the default executor; the waiting happens
        in asyncio.sleep, so many jobs can be in flight from one loop."""
        import asyncio

        loop = asyncio.get_running_loop()
        submit = await loop.run_in_executor(
            None, lambda: self._post(endpoint, payload))
        request_id = submit["request_id"]
        deadline = time.time() + self.timeout
        while time.time() < deadline:
            await asyncio.sleep(self.poll_interval)
            status = await loop.run_in_executor(
                None, lambda: self._get(f"/status/{request_id}"))
            state = status["state"]
            if state == "complete":
                return status["result"]
            elif state == "error":
                raise RemoteError(f"Remote evaluation failed: {status['error']}")
        raise Timeout(
            f"Remote evaluation timed out after {self.timeout}s "
            f"(request_id={request_id})"
        )

    def _get(self, endpoint):
        with urllib.request.urlopen(f"{self.server_url}{endpoint}") as resp:
            return json.loads(resp.read())

    def _submit(self, endpoint, payload):
        """POST a job payload to a queued endpoint and poll for its result."""
        req = urllib.request.Request(
            f"{self.server_url}{endpoint}",
            data=json.dumps(payload).encode(),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(req) as resp:
            submit_result = json.loads(resp.read())
        return _poll(self.server_url, submit_result["request_id"],
                     self.poll_interval, self.timeout)

    def _evaluate_remote(self, fta):
        """POST an FTA to /evaluate; returns the {"ftt", "perf"} reply."""
        return self._submit("/evaluate", {"fta": fta, "model": self.model_tag})

    def _post(self, endpoint, payload):
        req = urllib.request.Request(
            f"{self.server_url}{endpoint}",
            data=json.dumps(payload).encode(),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(req) as resp:
            return json.loads(resp.read())

    def set_seed(self, seed):
        """Seed the remote model's RNG (worker-side)."""
        self._post("/seed", {"seed": seed, "model": self.model_tag})

    def reset(self, kv=True):
        """Zero the remote model's counters (and optionally drop its KV)."""
        self._post("/reset", {"kv": kv, "model": self.model_tag})

    def capabilities(self):
        """The worker's routing surface: hosted models, pinning, sizes."""
        with urllib.request.urlopen(f"{self.server_url}/capabilities") as resp:
            return json.loads(resp.read())

    def score_frame(self, program, prompt_name, frame, content=None):
        """Engine.score_frame with the model work remote: instantiate and
        encode locally (the runtime is model-free), ship the text-level FTT
        to the worker's /score, return the scored FTT dict."""
        content = content or {}
        fta_id = self._sta.instantiate(
            program.id, prompt_name, content, self.syntax_id, self.search_id
        )
        try:
            ftt_id = self._sta.encode_frame(
                program.id, prompt_name, fta_id, frame, content
            )
            try:
                ftt = self._sta.get_ftt(ftt_id)
            finally:
                self._sta.release_ftt(ftt_id)
        finally:
            self._sta.release_fta(fta_id)
        reply = self._submit("/score", {"ftt": ftt, "model": self.model_tag})
        return reply["ftt"]

    def run(self, program, entry="main", externals=None, max_steps=100,
            recorder=None, **inputs):
        """Run a program, dispatching only the evaluation step to the backend
        (sync facade over run_async)."""
        import asyncio

        return asyncio.run(self.run_async(
            program, entry=entry, externals=externals, max_steps=max_steps,
            recorder=recorder, **inputs))

    async def run_async(self, program, entry="main", externals=None,
                        max_steps=100, recorder=None, **inputs):
        from .context import Context

        prompt = program.entry_prompt(entry)
        ctx = Context(program, self, prompt, inputs, externals or {},
                      recorder=recorder)
        steps = 0
        while not ctx.done and steps < max_steps:
            await ctx.step_async()
            steps += 1
        if not ctx.done:
            raise RemoteError(
                f"Program did not complete after {max_steps} steps "
                f"(at prompt '{ctx.prompt}')"
            )
        return ctx.result


class LaneLedger:
    """In-flight accounting for a set of worker lanes, keyed by URL.

    One ledger per model tag: EnginePools that differ only in syntax bind
    their own engines but share the ledger, so a worker's lane count is
    respected across all of them. Acquire blocks until a lane frees; an
    idle-lane preference for the last-served config key keeps per-config
    prefix warmth without ever delaying a job (heuristic only)."""

    def __init__(self, lanes_by_url):
        self.lanes = dict(lanes_by_url)      # url -> lane count
        self._inflight = {u: 0 for u in self.lanes}
        self._last_key = {}
        self._cv = None                      # created lazily on the loop

    def total_lanes(self):
        return sum(self.lanes.values())

    def _condition(self):
        import asyncio

        if self._cv is None:
            self._cv = asyncio.Condition()
        return self._cv

    async def acquire(self, key=None):
        cv = self._condition()
        async with cv:
            while True:
                chosen = None
                for url, lanes in self.lanes.items():
                    if self._inflight[url] < lanes:
                        if key is not None and self._last_key.get(url) == key:
                            chosen = url
                            break
                        chosen = chosen or url
                if chosen is not None:
                    self._inflight[chosen] += 1
                    if key is not None:
                        self._last_key[chosen] = key
                    return chosen
                await cv.wait()

    async def release(self, url):
        cv = self._condition()
        async with cv:
            self._inflight[url] -= 1
            cv.notify_all()


class EnginePool:
    """Virtual engine over N level-3 workers hosting one model tag.

    Drop-in Engine for Context: every evaluate/score job is dispatched to
    a free lane (worker in-flight ≤ its advertised "lanes", 1 today), so
    concurrent program executions — and a single execution's mapped-call
    fan-out — spread across all workers. A chain's steps hop lanes freely:
    prompts of one execution share no token prefix, and per-config prefix
    warmth establishes itself per worker after first touch.

    Engine surface parity: run/run_async, evaluate_prompt[_async],
    score_frame[_async], set_seed/reset (broadcast), capabilities (first
    worker). Must be used from a single event loop. Pass a shared
    LaneLedger when several pools (e.g. one per syntax) address the same
    workers — lane bounds are per worker, not per pool.
    """

    def __init__(self, urls, model_tag=None, syntax=None, search=None,
                 poll_interval=0.5, timeout=300, ledger=None):
        from .errors import ConfigError

        if not urls:
            raise ConfigError("EnginePool needs at least one worker URL")
        self.backends = {}                   # url -> RemoteBackend
        lanes = {}
        for u in urls:
            b = RemoteBackend(u, syntax=syntax, search=search,
                              poll_interval=poll_interval, timeout=timeout)
            b.model_tag = model_tag
            caps = b.capabilities()
            if model_tag is not None and model_tag not in caps["models"]:
                raise ConfigError(
                    f"worker {b.server_url} does not host {model_tag!r} "
                    f"(hosts: {caps['models']})")
            self.backends[b.server_url] = b
            lanes[b.server_url] = int(caps.get("lanes") or 1)
        self.ledger = ledger or LaneLedger(lanes)
        self.model_tag = model_tag
        first = next(iter(self.backends.values()))
        self.syntax_id = first.syntax_id
        self.search_id = first.search_id
        self.model_id = None
        self.last_perf = None

    def total_lanes(self):
        return self.ledger.total_lanes()

    def _backend_for(self, url):
        # A shared ledger may hand out a URL registered by a sibling pool
        # under a normalized form; ledgers built by pool_ledger use the
        # same normalization as RemoteBackend, so a plain lookup holds.
        return self.backends[url]

    async def evaluate_prompt_async(self, program, prompt_name, content,
                                    record_kinds=None):
        url = await self.ledger.acquire(
            key=(self.syntax_id, program.id, prompt_name))
        try:
            backend = self._backend_for(url)
            result = await backend.evaluate_prompt_async(
                program, prompt_name, content, record_kinds=record_kinds)
            self.last_perf = backend.last_perf
            return result
        finally:
            await self.ledger.release(url)

    async def score_frame_async(self, program, prompt_name, frame, content=None):
        url = await self.ledger.acquire(
            key=(self.syntax_id, program.id, prompt_name))
        try:
            return await self._backend_for(url).score_frame_async(
                program, prompt_name, frame, content=content)
        finally:
            await self.ledger.release(url)

    def evaluate_prompt(self, program, prompt_name, content, record_kinds=None):
        import asyncio

        return asyncio.run(self.evaluate_prompt_async(
            program, prompt_name, content, record_kinds=record_kinds))

    def score_frame(self, program, prompt_name, frame, content=None):
        import asyncio

        return asyncio.run(self.score_frame_async(
            program, prompt_name, frame, content=content))

    def run(self, program, entry="main", externals=None, max_steps=100,
            recorder=None, **inputs):
        import asyncio

        return asyncio.run(self.run_async(
            program, entry=entry, externals=externals, max_steps=max_steps,
            recorder=recorder, **inputs))

    async def run_async(self, program, entry="main", externals=None,
                        max_steps=100, recorder=None, **inputs):
        from .context import Context

        prompt = program.entry_prompt(entry)
        ctx = Context(program, self, prompt, inputs, externals or {},
                      recorder=recorder)
        steps = 0
        while not ctx.done and steps < max_steps:
            await ctx.step_async()
            steps += 1
        if not ctx.done:
            raise RemoteError(
                f"Program did not complete after {max_steps} steps "
                f"(at prompt '{ctx.prompt}')"
            )
        return ctx.result

    def set_seed(self, seed):
        for b in self.backends.values():
            b.set_seed(seed)

    def reset(self, kv=True):
        for b in self.backends.values():
            b.reset(kv)

    def capabilities(self):
        return next(iter(self.backends.values())).capabilities()


def lane_ledger(urls):
    """A LaneLedger over workers' advertised lane counts, keyed by the
    same normalized URL form RemoteBackend uses — share it between
    EnginePools that address the same workers under different syntaxes."""
    lanes = {}
    for u in urls:
        b = RemoteBackend(u)
        lanes[b.server_url] = int(b.capabilities().get("lanes") or 1)
    return LaneLedger(lanes)


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
