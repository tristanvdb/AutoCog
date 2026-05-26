"""
Engine — holds a model and syntax, drives program execution.
"""

import runtime_sta_cxx
import backend_llama_cxx

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

    def run(self, program, entry="main", externals=None, **inputs):
        """
        Run a program to completion.

        Args:
            program: compiled Program
            entry: entry point name (default: "main")
            externals: dict of name → callable for extern call channels
            **inputs: input channel values

        Returns:
            dict of return field values
        """
        prompt = program.entry_points.get(entry)
        if prompt is None:
            raise ValueError(f"Entry point '{entry}' not found in program")

        ctx = Context(program, self, prompt, inputs, externals or {})
        while not ctx.done:
            ctx.step()
        return ctx.result

    async def run_async(self, program, entry="main", externals=None, **inputs):
        """Async version of run — supports async external callables."""
        prompt = program.entry_points.get(entry)
        if prompt is None:
            raise ValueError(f"Entry point '{entry}' not found in program")

        ctx = Context(program, self, prompt, inputs, externals or {})
        while not ctx.done:
            await ctx.step_async()
        return ctx.result
