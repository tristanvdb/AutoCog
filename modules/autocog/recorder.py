"""Record execution artifacts and trace per context/step."""

import json
import os
import tempfile

from autocog.errors import ConfigError


class Recorder:
    """Tracks execution across contexts (main + sub-calls).

    Directory layout:
        {path}/ctx-{id}.json          — trace: prompt sequence, calls, flows
        {path}/ctx-{id}/{prompt}/{step}.json — step artifacts (input, frame, etc.)

    The trace file is updated after every event (flow, return, call) so it
    reflects progress even when execution crashes mid-way.
    """

    VALID_KINDS = {"input", "frame", "text", "fta", "ftt"}

    def __init__(self, kinds, path=None):
        if isinstance(kinds, str):
            kinds = {k.strip() for k in kinds.split(",")}
        invalid = kinds - self.VALID_KINDS
        if invalid:
            raise ConfigError(f"Invalid record kinds: {invalid}. Valid: {self.VALID_KINDS}")
        self.kinds = kinds
        self.path = path or tempfile.mkdtemp(prefix="autocog-record-")
        os.makedirs(self.path, exist_ok=True)
        self._next_ctx_id = 0

    # -- Context lifecycle --

    def new_context(self, entry, inputs=None, parent_ctx=None):
        """Allocate a new context ID, write initial trace file. Returns ctx_id."""
        ctx_id = self._next_ctx_id
        self._next_ctx_id += 1
        trace = {
            "ctx": ctx_id,
            "parent": parent_ctx,
            "entry": entry,
            "trace": [],
        }
        self._write_trace(ctx_id, trace)
        return ctx_id

    # -- Prompt-level events --

    def begin_prompt(self, ctx_id, prompt_name, step):
        """Append a new prompt entry to the trace."""
        trace = self._read_trace(ctx_id)
        trace["trace"].append({
            "prompt": prompt_name,
            "step": step,
            "calls": [],
        })
        self._write_trace(ctx_id, trace)

    def record_call(self, ctx_id, field, *, callable_name=None,
                    prompt_name=None, sub_ctx=None):
        """Record a channel call (extern or sub-prompt) on the current prompt."""
        trace = self._read_trace(ctx_id)
        entry = {"field": field}
        if callable_name is not None:
            entry["callable"] = callable_name
        if prompt_name is not None:
            entry["prompt"] = prompt_name
        if sub_ctx is not None:
            entry["context"] = sub_ctx
        trace["trace"][-1]["calls"].append(entry)
        self._write_trace(ctx_id, trace)

    def record_flow(self, ctx_id, next_prompt):
        """Record a flow transition on the current prompt."""
        trace = self._read_trace(ctx_id)
        if trace["trace"]:
            trace["trace"][-1]["next"] = next_prompt
        self._write_trace(ctx_id, trace)

    def record_return(self, ctx_id, result=None):
        """Finalize a context (return from prompt flow)."""
        trace = self._read_trace(ctx_id)
        trace["result"] = result
        self._write_trace(ctx_id, trace)

    # -- Step artifacts --

    def record_step(self, ctx_id, prompt_name, step, **artifacts):
        """Write step artifacts to ctx-{id}/{prompt}/{step}.json (and .txt for text)."""
        data = {}
        for kind in self.kinds:
            if kind == "text":
                continue  # text goes to a separate file
            if kind in artifacts and artifacts[kind] is not None:
                data[kind] = artifacts[kind]

        step_dir = os.path.join(self.path, f"ctx-{ctx_id}", prompt_name)
        os.makedirs(step_dir, exist_ok=True)

        if data:
            fpath = os.path.join(step_dir, f"{step}.json")
            with open(fpath, "w") as f:
                json.dump(data, f, indent=2, default=str)

        if "text" in self.kinds and "text" in artifacts and artifacts["text"] is not None:
            tpath = os.path.join(step_dir, f"{step}.txt")
            with open(tpath, "w") as f:
                f.write(artifacts["text"])

    # -- Internal --

    def _trace_path(self, ctx_id):
        return os.path.join(self.path, f"ctx-{ctx_id}.json")

    def _write_trace(self, ctx_id, trace):
        with open(self._trace_path(ctx_id), "w") as f:
            json.dump(trace, f, indent=2, default=str)

    def _read_trace(self, ctx_id):
        with open(self._trace_path(ctx_id)) as f:
            return json.load(f)
