"""
Engine — holds a model and syntax, drives program execution.
"""

import json

from autocog.runtime.sta import runtime_sta_cxx
from autocog.backend.llama import backend_llama_cxx

from .errors import ConfigError, OrchestrationError
from .context import Context


class Engine:
    """Execution engine: model + syntax."""

    def __init__(self, model=None, syntax=None, search=None, n_ctx=4096,
                 model_id=None):
        """
        Create an engine.

        Args:
            model: path to GGUF model file, or None for RNG model (id=0)
            syntax: path to syntax JSON file (required)
            search: path to search config JSON file (required)
            n_ctx: context size for the model
            model_id: share an already-loaded model (overrides `model`) —
                several engines differing only in syntax/search then reuse
                one set of weights instead of reloading per engine
        """
        if model_id is not None:
            self.model_id = model_id
        elif model is not None:
            self.model_id = backend_llama_cxx.create(model, n_ctx)
        else:
            self.model_id = 0  # RNG model

        if syntax is None:
            raise ConfigError("syntax is required — pass a path to a syntax JSON file")
        self.syntax_id = runtime_sta_cxx.load_syntax(syntax)

        if search is None:
            raise ConfigError("search is required — pass a path to a search config JSON file")
        self.search_id = runtime_sta_cxx.load_search(search)

        #: autocog.perf.* field map of the most recent prompt evaluation
        #: (per-evaluation deltas, same keys as `xfta --perf`), or None.
        self.last_perf = None

    def set_seed(self, seed):
        """Set the RNG seed for the underlying model."""
        backend_llama_cxx.set_seed(self.model_id, seed)

    def reset(self, kv=True):
        """Zero the model's accumulated kv/decode counters; with kv=True also
        drop every KV slot, so the next evaluation starts from a cold cache
        (measurement isolation between runs sharing this engine)."""
        backend_llama_cxx.reset(self.model_id, kv)

    def evaluate_prompt(self, program, prompt_name, content, record_kinds=None):
        """
        Evaluate a single prompt: instantiate → evaluate → walk FTT.

        Args:
            program: Program object
            prompt_name: name of the prompt to evaluate
            content: dict of resolved channel values
            record_kinds: set of artifact kinds to capture (or None)

        Returns:
            frame dict, or (frame, artifacts) if record_kinds is set
        """
        fta_id = runtime_sta_cxx.instantiate(
            program.id, prompt_name, content, self.syntax_id, self.search_id
        )
        artifacts = {}
        try:
            if record_kinds and "fta" in record_kinds:
                artifacts["fta"] = runtime_sta_cxx.get_fta(fta_id)

            ftt_id, perf_json = backend_llama_cxx.evaluate(self.model_id, fta_id)
            self.last_perf = json.loads(perf_json)
            if record_kinds and "perf" in record_kinds:
                artifacts["perf"] = self.last_perf
            try:
                # The backend stored the FTT; walk it from the store (model-free).
                frame = runtime_sta_cxx.walk_ftt_to_frame(
                    program.id, prompt_name, ftt_id, content
                )

                if record_kinds:
                    if "frame" in record_kinds:
                        artifacts["frame"] = frame
                    if "ftt" in record_kinds:
                        artifacts["ftt"] = runtime_sta_cxx.get_ftt(ftt_id)
            finally:
                runtime_sta_cxx.release_ftt(ftt_id)
        finally:
            runtime_sta_cxx.release_fta(fta_id)

        if record_kinds is not None:
            return frame, artifacts
        return frame

    def score_frame(self, program, prompt_name, frame, content=None):
        """Encode a frame into its canonical forced path under this engine's
        syntax and score every token against the model (the `efta --score`
        semantics). Returns the scored FTT as a dict.

        Args:
            program: Program object
            prompt_name: prompt the frame corresponds to
            frame: dict of field values (a recorded or constructed frame)
            content: input content dict; resolves select values back to
                indices when the frame holds resolved values
        """
        content = content or {}
        fta_id = runtime_sta_cxx.instantiate(
            program.id, prompt_name, content, self.syntax_id, self.search_id
        )
        try:
            ftt_id = runtime_sta_cxx.encode_frame(
                program.id, prompt_name, fta_id, frame, content
            )
            try:
                scored_id = backend_llama_cxx.score(self.model_id, ftt_id)
                try:
                    return runtime_sta_cxx.get_ftt(scored_id)
                finally:
                    runtime_sta_cxx.release_ftt(scored_id)
            finally:
                runtime_sta_cxx.release_ftt(ftt_id)
        finally:
            runtime_sta_cxx.release_fta(fta_id)

    def run(self, program, entry="main", externals=None, max_steps=100,
            recorder=None, **inputs):
        """
        Run a program to completion.

        Args:
            program: compiled Program
            entry: entry point name (default: "main")
            externals: dict of name → callable for extern call channels
            max_steps: maximum number of prompt steps (default: 100)
            recorder: Recorder instance for artifact capture (or None)
            **inputs: input channel values

        Returns:
            dict of return field values
        """
        prompt = program.entry_prompt(entry)

        ctx = Context(program, self, prompt, inputs, externals or {}, recorder=recorder)
        steps = 0
        while not ctx.done and steps < max_steps:
            ctx.step()
            steps += 1
        if not ctx.done:
            raise OrchestrationError(
                f"Program did not complete after {max_steps} steps "
                f"(at prompt '{ctx.prompt}'). Increase --max-steps if needed."
            )

        return ctx.result

    async def run_async(self, program, entry="main", externals=None, **inputs):
        """Async version of run — supports async external callables."""
        prompt = program.entry_prompt(entry)

        ctx = Context(program, self, prompt, inputs, externals or {})
        while not ctx.done:
            await ctx.step_async()
        return ctx.result
