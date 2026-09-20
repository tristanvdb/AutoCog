"""
Engine — holds a model and syntax, drives program execution.
"""

import json

from autocog.runtime.sta import runtime_sta_cxx
from autocog.backend.llama import backend_llama_cxx

from .errors import ConfigError, OrchestrationError
from .context import Context


def merge_content(content, frame):
    """Overlay a frame on the content it was generated against.

    Both are documents keyed by field name at the prompt's scope, so they
    overlap: a content-filled field is rendered into the prompt and therefore
    recorded in the frame too. Where both speak the frame wins — it holds the
    text that was actually rendered, which is the authority on what the model
    read.

    The merge is recursive because a record can be part content, part
    generated (an input sub-field beside a produced one); a shallow update
    would drop whichever side lost the key.
    """
    if not content:
        return frame
    if not isinstance(content, dict) or not isinstance(frame, dict):
        return frame
    merged = dict(content)
    for key, value in frame.items():
        if key in merged:
            merged[key] = merge_content(merged[key], value)
        else:
            merged[key] = value
    return merged


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
                # content seeds the frame: harmless here (this FTA is
                # unsimplified, so every field has a node) and correct if the
                # instantiation ever gains simplification, where fields the
                # content supplied have no node to walk.
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

    async def evaluate_prompt_async(self, program, prompt_name, content,
                                    record_kinds=None):
        """Async evaluation hook (awaited by Context.step_async). The local
        engine evaluates in-process and blocks the loop — it is one lane,
        there is nothing else to schedule. Pooled/remote engines override
        this with a genuinely awaitable dispatch."""
        return self.evaluate_prompt(program, prompt_name, content,
                                    record_kinds=record_kinds)

    def score_frame(self, program, prompt_name, frame, content=None):
        """Re-render a frame under this engine's syntax and score every token
        against the model. Returns the scored FTT as a dict.

        A complete frame supplied as content leaves the instantiator nothing
        to ask the model: every field takes its recorded value, so the FTA is
        one forced path of text actions, and `score_all` fills
        P(token | prefix) for every token on it. That is constraint friction —
        how hard the model fights the structure it never chose.

        Re-rendering is instantiation, which is why the syntax may be one the
        generating model never saw: the same renderer produces both.

        Args:
            program: Program object
            prompt_name: prompt the frame corresponds to
            frame: dict of field values (a recorded or constructed frame)
            content: the inputs the frame was generated against; merged under
                the frame, which wins wherever both speak
        """
        fta_id = runtime_sta_cxx.instantiate(
            program.id, prompt_name, merge_content(content, frame),
            self.syntax_id, self.search_id
        )
        try:
            ftt_id, _perf = backend_llama_cxx.evaluate(
                self.model_id, fta_id, score_all=True)
            try:
                return runtime_sta_cxx.get_ftt(ftt_id)
            finally:
                runtime_sta_cxx.release_ftt(ftt_id)
        finally:
            runtime_sta_cxx.release_fta(fta_id)

    def export_frame(self, program, prompt_name, frame, content=None,
                     simplify="full"):
        """Re-render a frame and return its tokens, for finetuning.

        A training example is the rendered prompt as the model would read it.
        This is the same re-rendering score_frame does, minus the scoring and
        plus simplification, which collapses the forced chain of text actions
        into the segments a loss wants to see.

        Returns a list of segments in order, each:

            {"uid", "field", "indices", "text", "tokens"}

        With simplify="full" that is one segment: the flat token list. With
        "limited" it alternates value and structure, every value segment
        carrying the schema field index and array indices it came from, so a
        weighted loss can penalize (say) only the generated values, or only
        the enumeration, instead of the whole sequence uniformly.

        These tokens are NOT the ones the runtime forces — merging text lets
        the tokenizer merge across the seams. That is what makes them right
        for training and wrong for friction, and why score_frame and this do
        not share a path.
        """
        fta_id = runtime_sta_cxx.instantiate(
            program.id, prompt_name, merge_content(content, frame),
            self.syntax_id, self.search_id, simplify=simplify
        )
        try:
            ftt_id, _perf = backend_llama_cxx.evaluate(self.model_id, fta_id)
            try:
                ftt = runtime_sta_cxx.get_ftt(ftt_id)
            finally:
                runtime_sta_cxx.release_ftt(ftt_id)
        finally:
            runtime_sta_cxx.release_fta(fta_id)

        segments, node = [], ftt
        while node is not None:
            segments.append({
                "uid": node["uid"],
                "field": node.get("field"),
                "indices": node.get("indices"),
                "text": node["text"],
                "tokens": node["tokens"],
            })
            kids = node.get("children") or []
            # Simplification merges text around decisions; it does not remove
            # them. A frame that left one open gives a tree, and taking the
            # first branch would quietly invent a training example the model
            # was never shown.
            if len(kids) > 1:
                raise ConfigError(
                    f"Cannot export: the frame left a decision open at "
                    f"{node.get('uid')!r} ({len(kids)} branches). A training "
                    f"example is one rendered prompt; supply a frame that "
                    f"determines every field.", path=node.get("uid"))
            node = kids[0] if kids else None
        return segments

    def run(self, program, entry="main", externals=None, max_steps=100,
            recorder=None, **inputs):
        """
        Run a program to completion (sync facade over run_async; callers
        already inside a running event loop must await run_async instead).

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
        import asyncio

        return asyncio.run(self.run_async(
            program, entry=entry, externals=externals, max_steps=max_steps,
            recorder=recorder, **inputs))

    async def run_async(self, program, entry="main", externals=None,
                        max_steps=100, recorder=None, **inputs):
        """Run a program to completion — the native implementation.
        Supports sync and async external callables; mapped calls fan out
        concurrently when the engine dispatch is awaitable.

        `externals=None` loads the Python callables the program names, from
        where it was compiled. A program that resolves its own answer would
        otherwise need every caller to remember to pass them, and forgetting
        is silent: the call returns None and the answer is quietly wrong.
        Pass a dict (even an empty one) to take over."""
        prompt = program.entry_prompt(entry)

        if externals is None:
            from .externals import load_externals
            externals = load_externals(program)

        ctx = Context(program, self, prompt, inputs, externals,
                      recorder=recorder)
        steps = 0
        while not ctx.done and steps < max_steps:
            await ctx.step_async()
            steps += 1
        if not ctx.done:
            raise OrchestrationError(
                f"Program did not complete after {max_steps} steps "
                f"(at prompt '{ctx.prompt}'). Increase --max-steps if needed."
            )

        return ctx.result
