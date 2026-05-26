"""
Context — execution state for one program invocation.
"""

from .channels import resolve_channels


class Context:
    """
    Execution context for a single program flow.

    Holds:
    - Current prompt name
    - Latest frame per prompt (dict of field values)
    - Branch counts (for flow limits)
    """

    def __init__(self, program, engine, prompt, inputs, externals=None):
        self.program = program
        self.engine = engine
        self.prompt = prompt
        self.inputs = inputs
        self.externals = externals or {}
        self.frames = {}    # prompt_name → latest frame dict
        self.branches = {}  # prompt_name → {target: count}
        self.done = False
        self.result = None

    def step(self):
        """Execute one prompt iteration."""
        if self.done:
            return

        # 1. Resolve channels → content dict (always local)
        content = resolve_channels(
            self.program, self.prompt,
            self.inputs, self.frames,
            self.engine, self.externals
        )

        # 2. Evaluate prompt → frame (dispatch: local or remote)
        frame = self.engine.evaluate_prompt(
            self.program, self.prompt, content
        )

        # 3. Store frame
        self.frames[self.prompt] = frame

        # 4. Flow control
        flows = self.program.prompt_flows(self.prompt)

        if not flows:
            # Terminal prompt: no flows → done
            self.done = True
            self.result = frame
            return

        flow_choice = frame.get("next", "")

        if flow_choice not in flows:
            # Default: if only one flow, use it
            if len(flows) == 1:
                flow_choice = next(iter(flows))
            else:
                raise RuntimeError(
                    f"Flow choice '{flow_choice}' not found in prompt '{self.prompt}'. "
                    f"Available: {list(flows.keys())}"
                )

        flow_def = flows[flow_choice]

        if flow_def["type"] == "return":
            # Extract return fields from frame
            self.result = self._extract_return(frame, flow_def)
            self.done = True
        elif flow_def["type"] == "control":
            # Check limit
            self.branches.setdefault(self.prompt, {})
            count = self.branches[self.prompt].get(flow_choice, 0)
            limit = flow_def.get("limit", 1)
            if count >= limit:
                raise RuntimeError(
                    f"Flow limit exceeded for '{flow_choice}' in '{self.prompt}' "
                    f"(count={count}, limit={limit})"
                )
            self.branches[self.prompt][flow_choice] = count + 1
            self.prompt = flow_def["prompt"]

    async def step_async(self):
        """Async version — supports async external callables."""
        # For now, just call sync version.
        # TODO: make channel resolution async for extern calls.
        self.step()

    def _extract_return(self, frame, flow_def):
        """Extract return fields from a frame according to the return definition."""
        fields = flow_def.get("fields", [])

        # Check for invalid mixing of _ with named aliases
        aliases = [f.get("alias", "_") for f in fields]
        has_root = "_" in aliases
        has_named = any(a != "_" for a in aliases)
        if has_root and has_named:
            raise RuntimeError("Invalid return: cannot mix '_' (root) with named aliases")

        if has_root and len(fields) == 1:
            # Single field with _ alias: return the value directly
            field = fields[0]
            path = field.get("path", [])
            if path:
                value = frame
                for step in path:
                    name = step.get("name")
                    if isinstance(value, dict) and name:
                        value = value.get(name)
                    else:
                        value = None
                        break
                return value
            else:
                return frame.get("_")

        # Multiple named fields: return a dict
        result = {}
        for field in fields:
            alias = field.get("alias", "_")
            path = field.get("path", [])
            if path:
                value = frame
                for step in path:
                    name = step.get("name")
                    if isinstance(value, dict) and name:
                        value = value.get(name)
                    else:
                        value = None
                        break
                result[alias] = value
            else:
                result[alias] = frame.get(alias)
        return result
