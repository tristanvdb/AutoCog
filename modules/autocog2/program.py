"""
Program — compiled STA, wraps a ProgramID.
"""

import compiler_stl_cxx


class Program:
    """A compiled STL program. Wraps an opaque ProgramID."""

    def __init__(self, program_id):
        self.id = program_id
        self._sta = None

    @property
    def sta(self):
        """Lazily load the STA dict for channel inspection."""
        if self._sta is None:
            self._sta = compiler_stl_cxx.emit(self.id, "sta")
        return self._sta

    @property
    def entry_points(self):
        return self.sta["entry_points"]

    @property
    def prompts(self):
        return self.sta["prompts"]

    def prompt_channels(self, prompt_name):
        return self.prompts[prompt_name].get("channels", [])

    def prompt_flows(self, prompt_name):
        return self.prompts[prompt_name].get("flows", {})

    def __del__(self):
        if hasattr(self, 'id') and self.id is not None:
            try:
                compiler_stl_cxx.release(self.id)
            except Exception:
                pass


def compile(filepath, includes=None, entry_points=None):
    """Compile an STL file into a Program."""
    pid = compiler_stl_cxx.compile(
        filepath,
        includes=includes or [],
        entry_points=entry_points or ["main"]
    )
    return Program(pid)
