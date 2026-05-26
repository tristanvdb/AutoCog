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

    def evaluate_prompt(self, program, prompt_name, content):
        """
        Evaluate a single prompt: instantiate → evaluate → parse.

        This is the dispatch point for local vs remote execution.
        LocalEngine (this class) calls C++ bindings directly.
        RemoteEngine would send an HTTP request instead.

        Args:
            program: Program object
            prompt_name: name of the prompt to evaluate
            content: dict of resolved channel values

        Returns:
            frame dict (parsed field values including 'next' for flow)
        """
        fta_id = runtime_sta_cxx.instantiate(
            program.id, prompt_name, content, self.syntax_id
        )
        try:
            ftt_id = backend_llama_cxx.evaluate(self.model_id, fta_id)
            try:
                paths = backend_llama_cxx.get_best(self.model_id, ftt_id, 1)
                text = paths[0] if paths else ""
                frame = runtime_sta_cxx.parse_text(
                    program.id, prompt_name,
                    self.syntax_id, text, content
                )
            finally:
                backend_llama_cxx.release_ftt(ftt_id)
        finally:
            runtime_sta_cxx.release_fta(fta_id)
        return frame

    def run(self, program, entry="main", externals=None, max_steps=100, **inputs):
        """
        Run a program to completion.

        Args:
            program: compiled Program
            entry: entry point name (default: "main")
            externals: dict of name → callable for extern call channels
            max_steps: maximum number of prompt steps (default: 100)
            **inputs: input channel values

        Returns:
            dict of return field values
        """
        prompt = program.entry_prompt(entry)

        ctx = Context(program, self, prompt, inputs, externals or {})
        steps = 0
        while not ctx.done and steps < max_steps:
            ctx.step()
            steps += 1
        if not ctx.done:
            raise RuntimeError(
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
