"""
Program — compiled or loaded STA, wraps a ProgramID.
"""

import os

from autocog.compiler.stl import compiler_stl_cxx
from autocog.runtime.sta import runtime_sta_cxx


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
        """Map of entry name → {prompt, inputs, outputs}."""
        return self.sta["entry_points"]

    def entry_prompt(self, entry="main"):
        """Get the prompt name for an entry point."""
        ep = self.entry_points.get(entry)
        if ep is None:
            raise ValueError(f"Entry point '{entry}' not found")
        return ep["prompt"]

    def input_schema(self, entry="main"):
        """Get the input schema for an entry point."""
        return self.entry_points.get(entry, {}).get("inputs", {})

    def output_schema(self, entry="main"):
        """Get the output schema for an entry point."""
        return self.entry_points.get(entry, {}).get("outputs", {})

    @property
    def prompts(self):
        return self.sta["prompts"]

    @property
    def python_imports(self):
        """Map of extern_name → {file, target} for Python callables."""
        return self.sta.get("python_imports", {})

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


def _stdlib_path():
    """Return the path to the installed stlib directory."""
    pkg_dir = os.path.dirname(os.path.abspath(__file__))
    stlib = os.path.join(pkg_dir, "stlib")
    if os.path.isdir(stlib):
        return stlib
    # Fallback: development layout (share/library/stlib)
    repo = os.path.dirname(os.path.dirname(pkg_dir))
    stlib = os.path.join(repo, "share", "library", "stlib")
    if os.path.isdir(stlib):
        return stlib
    return None


def _default_syntax_path():
    """Return the path to the default syntax JSON file."""
    pkg_dir = os.path.dirname(os.path.abspath(__file__))
    # Installed layout: autocog/syntax/default.json
    candidate = os.path.join(pkg_dir, "syntax", "default.json")
    if os.path.isfile(candidate):
        return candidate
    # Dev layout: share/syntax/default.json
    repo = os.path.dirname(os.path.dirname(pkg_dir))
    candidate = os.path.join(repo, "share", "syntax", "default.json")
    if os.path.isfile(candidate):
        return candidate
    return None


def compile(filepath, includes=None, entry_points=None):
    """Compile an STL file into a Program."""
    inc = list(includes or [])
    # Add stdlib as implicit include (lowest priority)
    stdlib = _stdlib_path()
    if stdlib and stdlib not in inc:
        inc.append(stdlib)
    pid = compiler_stl_cxx.compile(
        filepath,
        includes=inc,
        entry_points=entry_points or ["main"]
    )
    return Program(pid)


def load(filepath):
    """Load a pre-compiled STA JSON file into a Program."""
    pid = runtime_sta_cxx.load_program(filepath)
    return Program(pid)
