"""
Program — compiled or loaded STA, wraps a ProgramID.
"""

import logging
import os

from autocog.compiler.stl import compiler_stl_cxx
from autocog.runtime.sta import runtime_sta_cxx
from autocog.errors import (
    ConfigError, CompileError, FileError, Diagnostic, SourceLocation,
)

log = logging.getLogger("autocog")

# Map a diagnostic level name to a Python logging level. Errors log at ERROR
# (the recoverable tier); the presence of any error then raises a CompileError,
# which a server loop may recover from or main turns into a fatal exit.
_DIAG_LOG_LEVEL = {
    "error": logging.ERROR,
    "warning": logging.WARNING,
    "note": logging.INFO,
}


def _to_diagnostic(record):
    """Translate a raw diagnostic dict (from the binding) into a Diagnostic."""
    loc = record.get("location")
    location = None
    if loc is not None:
        location = SourceLocation(
            file=loc["file"], line=loc["line"], column=loc["column"],
        )
    return Diagnostic(
        level=record.get("level", "error"),
        message=record.get("message", ""),
        location=location,
        source_line=record.get("source_line"),
        notes=list(record.get("notes") or []),
    )


def _process_diagnostics(program_id):
    """Retrieve, translate, and log the diagnostics for a compiled program.

    Logs each diagnostic at its mapped level, then — if any are errors —
    releases the (unusable) program and raises CompileError carrying the full
    list. Returns the diagnostics list when there are no errors.
    """
    raw = compiler_stl_cxx.get_diagnostics(program_id)
    diagnostics = [_to_diagnostic(r) for r in raw]

    for d in diagnostics:
        log.log(_DIAG_LOG_LEVEL.get(d.level, logging.ERROR), "%s", d)

    errors = [d for d in diagnostics if d.level == "error"]
    if errors:
        # The program is unusable; drop it before raising.
        compiler_stl_cxx.release(program_id)
        first = errors[0]
        raise CompileError(
            f"compilation failed with {len(errors)} error(s)",
            location=first.location,
            source_line=first.source_line,
            notes=first.notes,
            diagnostics=diagnostics,
        )
    return diagnostics


class Program:
    """A compiled STL program. Wraps an opaque ProgramID."""

    def __init__(self, program_id):
        self.id = program_id
        self._sta = None

    @property
    def sta(self):
        """Lazily cache the STA dict for channel inspection.

        The dict is a translation of the C++ structure handed back by the
        binding (get_sta); Python never parses STA JSON itself. Treated as a
        read-only view of the immutable datastore entry.
        """
        if self._sta is None:
            self._sta = runtime_sta_cxx.get_sta(self.id)
        return self._sta

    def dump_json(self):
        """Serialize the STA to a JSON string via C++ (single serializer)."""
        return runtime_sta_cxx.dump_sta(self.id)

    def store(self, path):
        """Serialize the STA to a file via C++."""
        runtime_sta_cxx.store_sta(self.id, path)

    @property
    def entry_points(self):
        """Map of entry name → {prompt, inputs, outputs}."""
        return self.sta["entry_points"]

    def entry_prompt(self, entry="main"):
        """Get the prompt name for an entry point."""
        ep = self.entry_points.get(entry)
        if ep is None:
            raise ConfigError(f"Entry point '{entry}' not found")
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


def _default_search_path():
    """Return the path to the default search config JSON file."""
    pkg_dir = os.path.dirname(os.path.abspath(__file__))
    # Installed layout: autocog/search/default.json
    candidate = os.path.join(pkg_dir, "search", "default.json")
    if os.path.isfile(candidate):
        return candidate
    # Dev layout: share/search/default.json
    repo = os.path.dirname(os.path.dirname(pkg_dir))
    candidate = os.path.join(repo, "share", "search", "default.json")
    if os.path.isfile(candidate):
        return candidate
    return None


def compile(filepath, includes=None, entry_points=None):
    """Compile an STL file into a Program."""
    # A missing top-level input is a file error, not a compile diagnostic:
    # there is no source to point into. (Imports that fail to resolve are
    # reported as located CompileError diagnostics by the compiler instead.)
    if not os.path.isfile(filepath):
        raise FileError(f"Cannot find file: {filepath}", path=filepath)

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
    # Log diagnostics and raise CompileError if the compile produced errors
    # (this releases the unusable program before raising).
    _process_diagnostics(pid)
    return Program(pid)


def load(filepath):
    """Load a pre-compiled STA JSON file into a Program."""
    pid = runtime_sta_cxx.load_sta(filepath)
    return Program(pid)
