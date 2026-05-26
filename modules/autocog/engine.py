"""
Engine — holds a model and syntax, drives program execution.
"""

from autocog.runtime.sta import runtime_sta_cxx
from autocog.backend.llama import backend_llama_cxx

from .context import Context


class Engine:
    """Execution engine: model + syntax."""

    def __init__(self, model=None, syntax=None, n_ctx=4096):
        """
        Create an engine.

        Args:
            model: path to GGUF model file, or None for RNG model (id=0)
            syntax: path to syntax JSON file
            n_ctx: context size for the model
        """
        if model is not None:
            self.model_id = backend_llama_cxx.create(model, n_ctx)
        else:
            self.model_id = 0  # RNG model

        if syntax is not None:
            self.syntax_id = runtime_sta_cxx.load_syntax(syntax)
        else:
            raise ValueError("syntax is required — pass a path to a syntax JSON file")

    def evaluate_prompt(self, program, prompt_name, content, record_kinds=None):
        """
        Evaluate a single prompt: instantiate → evaluate → parse.

        Args:
            program: Program object
            prompt_name: name of the prompt to evaluate
            content: dict of resolved channel values
            record_kinds: set of artifact kinds to capture (or None)

        Returns:
            frame dict, or (frame, artifacts) if record_kinds is set
        """
        fta_id = runtime_sta_cxx.instantiate(
            program.id, prompt_name, content, self.syntax_id
        )
        artifacts = {}
        try:
            if record_kinds and "fta" in record_kinds:
                import json as json_mod
                artifacts["fta"] = json_mod.loads(runtime_sta_cxx.get_fta_json(fta_id))

            ftt_id = backend_llama_cxx.evaluate(self.model_id, fta_id)
            try:
                paths = backend_llama_cxx.get_best(self.model_id, ftt_id, 1)
                text = paths[0] if paths else ""
                frame = runtime_sta_cxx.parse_text(
                    program.id, prompt_name,
                    self.syntax_id, text, content
                )
                if record_kinds:
                    if "text" in record_kinds:
                        artifacts["text"] = text
                    if "frame" in record_kinds:
                        artifacts["frame"] = frame
                    if "ftt" in record_kinds:
                        import json as json_mod
                        artifacts["ftt"] = json_mod.loads(
                            backend_llama_cxx.get_ftt_json(self.model_id, ftt_id)
                        )
            finally:
                backend_llama_cxx.release_ftt(ftt_id)
        finally:
            runtime_sta_cxx.release_fta(fta_id)

        if record_kinds is not None:
            return frame, artifacts
        return frame

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
            raise RuntimeError(
                f"Program did not complete after {max_steps} steps "
                f"(at prompt '{ctx.prompt}'). Increase --max-steps if needed."
            )

        if recorder:
            recorder.finalize(entry=entry, inputs=inputs, outputs=ctx.result)

        return ctx.result

    async def run_async(self, program, entry="main", externals=None, **inputs):
        """Async version of run — supports async external callables."""
        prompt = program.entry_prompt(entry)

        ctx = Context(program, self, prompt, inputs, externals or {})
        while not ctx.done:
            await ctx.step_async()
        return ctx.result
