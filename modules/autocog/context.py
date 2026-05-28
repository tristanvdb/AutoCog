"""
Context — execution state for one program invocation.
"""

from .channels import resolve_channels
from .errors import OrchestrationError


class Context:
    """
    Execution context for a single program flow.

    Holds:
    - Current prompt name
    - Latest frame per prompt (dict of field values)
    - Branch counts (for flow limits)
    """

    def __init__(self, program, engine, prompt, inputs, externals=None,
                 recorder=None, parent_ctx=None):
        self.program = program
        self.engine = engine
        self.prompt = prompt
        self.inputs = inputs
        self.externals = externals or {}
        self.recorder = recorder
        self.frames = {}    # prompt_name → latest frame dict
        self.branches = {}  # prompt_name → {target: count}
        self.done = False
        self.result = None
        self._step_count = 0

        # Register context with recorder
        if recorder:
            self.ctx_id = recorder.new_context(
                entry=prompt, inputs=inputs, parent_ctx=parent_ctx
            )
        else:
            self.ctx_id = None

    def step(self):
        """Execute one prompt iteration."""
        if self.done:
            return

        # Record prompt begin
        if self.recorder:
            self.recorder.begin_prompt(self.ctx_id, self.prompt, self._step_count)

        # 1. Resolve channels → content dict
        content = resolve_channels(
            self.program, self.prompt,
            self.inputs, self.frames,
            self.engine, self.externals,
            recorder=self.recorder, ctx_id=self.ctx_id
        )

        # 2. Evaluate prompt → frame
        if self.recorder:
            frame, artifacts = self.engine.evaluate_prompt(
                self.program, self.prompt, content,
                record_kinds=self.recorder.kinds
            )
            self.recorder.record_step(
                self.ctx_id, self.prompt, self._step_count,
                input=content if "input" in self.recorder.kinds else None,
                frame=artifacts.get("frame"),
                text=artifacts.get("text"),
                fta=artifacts.get("fta"),
                ftt=artifacts.get("ftt"),
            )
        else:
            frame = self.engine.evaluate_prompt(
                self.program, self.prompt, content
            )
        self._step_count += 1

        # 3. Store frame
        self.frames[self.prompt] = frame

        # 4. Flow control
        flows = self.program.prompt_flows(self.prompt)

        if not flows:
            # Terminal prompt: no flows → done
            self.done = True
            self.result = frame
            if self.recorder:
                self.recorder.record_return(self.ctx_id, self.result)
            return

        flow_choice = frame.get("next", "")

        if flow_choice not in flows:
            raise OrchestrationError(
                f"Flow choice '{flow_choice}' not found in prompt '{self.prompt}'. "
                f"Available: {list(flows.keys())}. "
                f"This usually means the parser failed to extract the 'next' field."
            )

        flow_def = flows[flow_choice]

        if flow_def["type"] == "return":
            self.result = self._extract_return(frame, flow_def)
            self.done = True
            if self.recorder:
                self.recorder.record_return(self.ctx_id, self.result)
        elif flow_def["type"] == "control":
            # Check limit (only for flows with explicit limits)
            self.branches.setdefault(self.prompt, {})
            count = self.branches[self.prompt].get(flow_choice, 0)
            limit = flow_def.get("limit")
            if limit is not None and count >= limit:
                raise OrchestrationError(
                    f"Flow limit exceeded for '{flow_choice}' in '{self.prompt}' "
                    f"(count={count}, limit={limit})"
                )
            self.branches[self.prompt][flow_choice] = count + 1
            if self.recorder:
                self.recorder.record_flow(self.ctx_id, flow_def["prompt"])
            self.prompt = flow_def["prompt"]

    async def step_async(self):
        """Async version — supports async external callables."""
        self.step()

    def _extract_return(self, frame, flow_def):
        """Extract return fields from a frame according to the return definition."""
        fields = flow_def.get("fields", [])

        aliases = [f.get("alias", "_") for f in fields]
        has_root = "_" in aliases
        has_named = any(a != "_" for a in aliases)
        if has_root and has_named:
            raise OrchestrationError("Invalid return: cannot mix '_' (root) with named aliases")

        if has_root and len(fields) == 1:
            field = fields[0]
            path = field.get("path", [])
            return self._resolve_path(frame, path) if path else frame.get("_")

        result = {}
        for field in fields:
            alias = field.get("alias", "_")
            path = field.get("path", [])
            result[alias] = self._resolve_path(frame, path) if path else frame.get(alias)
        return result

    def _resolve_path(self, value, path):
        """Walk a dotted path through nested dicts/lists.

        When a path step hits a list, map the remaining steps over each
        element and flatten. This handles ``return use pages.page`` where
        ``pages`` is an array of records each containing a ``page`` field.
        """
        from .errors import ExecutionError

        for i, step in enumerate(path):
            name = step.get("name")
            if name is None:
                raise ExecutionError(
                    f"Return path step {i} has no 'name' field")

            if isinstance(value, dict):
                if name not in value:
                    raise ExecutionError(
                        f"Return path '{self._fmt_path(path)}': "
                        f"field '{name}' not found in dict at step {i}. "
                        f"Available: {list(value.keys())}")
                value = value[name]
            elif isinstance(value, list):
                remaining = path[i:]
                result = []
                for j, elem in enumerate(value):
                    try:
                        result.append(self._resolve_path(elem, remaining))
                    except ExecutionError as e:
                        raise ExecutionError(
                            f"Return path '{self._fmt_path(path)}': "
                            f"error at list index {j}: {e}") from e
                return result
            else:
                raise ExecutionError(
                    f"Return path '{self._fmt_path(path)}': "
                    f"expected dict or list at step {i} ('{name}'), "
                    f"got {type(value).__name__}")
        return value

    @staticmethod
    def _fmt_path(path):
        return ".".join(s.get("name", "?") for s in path)
