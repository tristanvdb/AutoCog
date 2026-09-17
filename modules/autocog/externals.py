"""Loading the Python callables a program names.

A program that resolves its own output — `return call pick _;` — names a
Python function, and every runner needs it: the CLI, `autocog bench`, a
campaign, a test. This used to live in `__main__`, which meant only the CLI
could run such a program; everything else silently got `None` back from the
call. Programs now routinely carry a resolver, so loading them belongs to the
library.
"""

import importlib.util
import os
import sys


def load_externals(program, include_paths=None):
    """Load the Python callables a program's `from "x.py" import f` names.

    Searches `include_paths`, then the paths the program was compiled with
    (its source directory and includes), then the stdlib. A name that cannot
    be resolved is warned about and skipped rather than raised: the CLI has
    always behaved that way, and a program may legitimately be inspected
    without its resolvers present.
    """
    from .program import _stdlib_path

    search_paths = list(include_paths or [])
    for path in getattr(program, "search_paths", []):
        if path not in search_paths:
            search_paths.append(path)
    stdlib = _stdlib_path()
    if stdlib and stdlib not in search_paths:
        search_paths.append(stdlib)

    externals = {}
    loaded_modules = {}

    for extern_name, info in program.python_imports.items():
        py_file = info["file"]
        target_func = info["target"]

        module_path = None
        for inc in search_paths:
            candidate = os.path.join(inc, py_file)
            if os.path.isfile(candidate):
                module_path = candidate
                break

        if module_path is None:
            print(f"Warning: Python file '{py_file}' not found in include paths",
                  file=sys.stderr)
            continue

        if module_path not in loaded_modules:
            mod_name = os.path.splitext(os.path.basename(py_file))[0]
            spec = importlib.util.spec_from_file_location(mod_name, module_path)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            loaded_modules[module_path] = mod

        func = getattr(loaded_modules[module_path], target_func, None)
        if func is None:
            print(f"Warning: function '{target_func}' not found in '{py_file}'",
                  file=sys.stderr)
            continue

        externals[extern_name] = func

    return externals
