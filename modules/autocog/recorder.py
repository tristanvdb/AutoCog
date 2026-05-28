from autocog.errors import ConfigError
"""Record execution artifacts (input, frame, text, FTA, FTT) per step."""

import json
import os
import tempfile


class Recorder:
    """Captures artifacts during program execution.

    Usage:
        recorder = Recorder(kinds={"input", "frame", "text"}, path="/tmp/recording")
        # ... pass to engine/context ...
        recorder.finalize(context_id=0, entry="main", inputs={...}, outputs={...})
    """

    VALID_KINDS = {"input", "frame", "text", "fta", "ftt"}

    def __init__(self, kinds, path=None):
        if isinstance(kinds, str):
            kinds = {k.strip() for k in kinds.split(",")}
        invalid = kinds - self.VALID_KINDS
        if invalid:
            raise ConfigError(f"Invalid record kinds: {invalid}. Valid: {self.VALID_KINDS}")
        self.kinds = kinds
        if path:
            self.path = path
            os.makedirs(path, exist_ok=True)
        else:
            self.path = tempfile.mkdtemp(prefix="autocog-record-")
        self.steps = []       # list of step names: "prompt_name-stack_depth"
        self.context_id = 0

    def record_step(self, prompt_name, stack_depth, *, input=None, frame=None,
                    text=None, fta=None, ftt=None):
        """Record artifacts for a single step."""
        step_name = f"{prompt_name}-{stack_depth}"
        self.steps.append(step_name)

        prefix = f"ctx{self.context_id}-{step_name}"

        # Build JSON with requested fields
        data = {}
        if "input" in self.kinds and input is not None:
            data["input"] = input
        if "frame" in self.kinds and frame is not None:
            data["frame"] = frame
        if "fta" in self.kinds and fta is not None:
            data["fta"] = fta
        if "ftt" in self.kinds and ftt is not None:
            data["ftt"] = ftt

        if data:
            json_path = os.path.join(self.path, f"{prefix}.json")
            with open(json_path, "w") as f:
                json.dump(data, f, indent=2, default=str)

        if "text" in self.kinds and text is not None:
            text_path = os.path.join(self.path, f"{prefix}.txt")
            with open(text_path, "w") as f:
                f.write(text)

    def finalize(self, entry, inputs, outputs):
        """Write the context-level metadata file."""
        ctx = {
            "context_id": self.context_id,
            "entry": entry,
            "inputs": inputs,
            "outputs": outputs,
            "steps": self.steps,
        }
        ctx_path = os.path.join(self.path, f"ctx{self.context_id}.json")
        with open(ctx_path, "w") as f:
            json.dump(ctx, f, indent=2, default=str)
