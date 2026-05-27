"""
Engine — holds a model and syntax, drives program execution.
"""

from autocog.runtime.sta import runtime_sta_cxx
from autocog.backend.llama import backend_llama_cxx

from .context import Context


class Engine:
    """Execution engine: model + syntax."""

    def __init__(self, model=None, syntax=None, search=None, n_ctx=4096):
        """
        Create an engine.

        Args:
            model: path to GGUF model file, or None for RNG model (id=0)
            syntax: path to syntax JSON file (required)
            search: path to search config JSON file (required)
            n_ctx: context size for the model
        """
        if model is not None:
            self.model_id = backend_llama_cxx.create(model, n_ctx)
        else:
            self.model_id = 0  # RNG model

        if syntax is None:
            raise ValueError("syntax is required — pass a path to a syntax JSON file")
        self.syntax_id = runtime_sta_cxx.load_syntax(syntax)

        if search is None:
            raise ValueError("search is required — pass a path to a search config JSON file")
        self.search_id = runtime_sta_cxx.load_search_config(search)

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
        import json as json_mod

        fta_id = runtime_sta_cxx.instantiate(
            program.id, prompt_name, content, self.syntax_id, self.search_id
        )
        artifacts = {}
        try:
            if record_kinds and "fta" in record_kinds:
                artifacts["fta"] = json_mod.loads(runtime_sta_cxx.get_fta_json(fta_id))

            ftt_id = backend_llama_cxx.evaluate(self.model_id, fta_id)
            try:
                # Get FTT with uid/field/indices metadata
                ftt = json_mod.loads(
                    backend_llama_cxx.get_ftt_json(self.model_id, fta_id, ftt_id)
                )

                # Walk best path and extract field values
                frame, text = _walk_ftt(ftt, program, prompt_name, content)

                if record_kinds:
                    if "text" in record_kinds:
                        artifacts["text"] = text
                    if "frame" in record_kinds:
                        artifacts["frame"] = frame
                    if "ftt" in record_kinds:
                        artifacts["ftt"] = ftt
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


def _walk_ftt(ftt_node, program, prompt_name, content):
    """Walk the FTT tree following best (first non-pruned) path.

    Returns (frame, text):
        frame: dict of field name → value (nested for records/arrays)
        text: concatenated text from the path
    """
    # Collect path: list of FTT nodes from root to leaf
    path = []
    node = ftt_node
    while True:
        path.append(node)
        children = [c for c in node.get("children", []) if not c.get("pruned", False)]
        if not children:
            break
        node = children[0]  # best = first non-pruned

    # Get STA field info
    sta = program.sta
    prompt_sta = sta["prompts"][prompt_name]
    fields = prompt_sta["fields"]

    # Build frame from path
    frame = {}
    text_parts = []

    for node in path:
        text_parts.append(node.get("text", ""))

        if "field" not in node:
            continue  # header, branch, endl, skip — no value

        field_idx = node["field"]
        indices = node.get("indices", [])
        fld = fields[field_idx]
        name = fld["name"]
        value = node.get("text", "")

        # Strip trailing whitespace from values
        value = value.rstrip("\n\r")

        # Resolve select: convert index to actual value
        fmt = fld.get("format", {})
        if fmt and fmt.get("type") == "choice" and fmt.get("mode") == "select":
            value = _resolve_select(value, fmt, content)

        # Store in frame using field hierarchy
        _set_field_value(frame, fields, field_idx, indices, value)

    text = "".join(text_parts)
    return frame, text


def _resolve_select(index_str, fmt, content):
    """Resolve a select index to the actual value from content."""
    try:
        idx = int(index_str)
        # Walk the choice path to find the source array
        path = fmt.get("path", [])
        current = content
        for step in path:
            name = step["name"]
            if isinstance(current, dict):
                current = current.get(name)
            elif isinstance(current, list):
                # For array fields, collect all values of this sub-field
                current = [item.get(name) if isinstance(item, dict) else item
                           for item in current]
            if current is None:
                return index_str
        if isinstance(current, list) and 0 <= idx < len(current):
            return current[idx]
    except (ValueError, TypeError, KeyError, IndexError):
        pass
    return index_str


def _set_field_value(frame, fields, field_idx, indices, value):
    """Set a value in the nested frame dict using field hierarchy."""
    fld = fields[field_idx]
    is_list = fld.get("range") is not None

    if fld["depth"] == 1:
        if is_list and indices:
            arr_idx = indices[-1]
            if fld["name"] not in frame:
                frame[fld["name"]] = []
            while len(frame[fld["name"]]) <= arr_idx:
                frame[fld["name"]].append(None)
            frame[fld["name"]][arr_idx] = value
        else:
            frame[fld["name"]] = value
    else:
        # Nested: build ancestor chain child→root
        chain = []
        idx = field_idx
        idx_cursor = len(indices)

        while idx >= 0:
            f = fields[idx]
            f_is_list = f.get("range") is not None
            # Every field consumes one index from the end
            idx_cursor -= 1
            arr_idx = indices[idx_cursor] if idx_cursor >= 0 else 0
            if f_is_list:
                chain.append((f["name"], arr_idx))
            else:
                chain.append((f["name"], None))
            if f["depth"] == 1:
                break
            parent_idx = idx - 1
            while parent_idx >= 0 and fields[parent_idx]["depth"] >= f["depth"]:
                parent_idx -= 1
            idx = parent_idx

        chain.reverse()

        current = frame
        for i, (n, arr_idx) in enumerate(chain):
            is_last = (i == len(chain) - 1)
            if is_last:
                if arr_idx is not None:
                    if n not in current:
                        current[n] = []
                    while len(current[n]) <= arr_idx:
                        current[n].append(None)
                    current[n][arr_idx] = value
                else:
                    current[n] = value
            else:
                if arr_idx is not None:
                    if n not in current:
                        current[n] = []
                    while len(current[n]) <= arr_idx:
                        current[n].append({})
                    current = current[n][arr_idx]
                else:
                    if n not in current:
                        current[n] = {}
                    current = current[n]
