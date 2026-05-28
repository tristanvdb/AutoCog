"""
.stapp packaging — create and load Structured Thought App packages.

A .stapp is a zip file containing:
  manifest.json     — metadata, entry points, schemas
  program.sta.json  — pre-compiled STA (optional)
  source.stl        — STL source (optional)
  externals/        — Python external files
  stl/              — imported STL files (non-stdlib)
"""

import json
import os
import shutil
import tempfile
import zipfile

from .errors import ConfigError


def _collect_app_files(program, include_paths, stl_path=None):
    """Collect app-specific files (externals + STL imports) from include paths.

    Returns:
      externals: {filename: absolute_path}
      stl_imports: {filename: absolute_path}
    """
    from .program import _stdlib_path
    stdlib = _stdlib_path()

    externals = {}
    for name, info in program.python_imports.items():
        py_file = info["file"]
        for inc in include_paths:
            candidate = os.path.join(inc, py_file)
            if os.path.isfile(candidate):
                # Skip stdlib files
                if stdlib and os.path.abspath(candidate).startswith(os.path.abspath(stdlib)):
                    break
                externals[py_file] = os.path.abspath(candidate)
                break

    # Collect non-stdlib STL imports by scanning include paths for .stl files
    # that are referenced by the program (they appear in the STA prompts as
    # source files, but we don't have that info directly — collect all .stl
    # files from app include paths)
    stl_imports = {}
    main_stl = os.path.basename(stl_path) if stl_path else None
    for inc in include_paths:
        if stdlib and os.path.abspath(inc) == os.path.abspath(stdlib):
            continue
        for fname in os.listdir(inc):
            if fname.endswith(".stl") and fname != main_stl:
                stl_imports[fname] = os.path.join(inc, fname)

    return externals, stl_imports


def _merge_schema_overrides(entry_points, schema_path):
    """Merge author-provided schema overrides into entry_points.

    The override file mirrors the entry_points structure but only
    includes fields to add/update (descriptions, defaults, etc.):

        {
          "main": {
            "inputs": {
              "query": {"description": "What kind of story"},
              "age": {"description": "Target age", "default": "5"}
            }
          }
        }
    """
    with open(schema_path) as f:
        overrides = json.load(f)

    for entry_name, entry_overrides in overrides.items():
        if entry_name not in entry_points:
            continue
        ep = entry_points[entry_name]
        for section in ("inputs", "outputs"):
            if section not in entry_overrides:
                continue
            if section not in ep:
                ep[section] = {}
            for field_name, field_overrides in entry_overrides[section].items():
                if field_name in ep[section]:
                    ep[section][field_name].update(field_overrides)


def pack(stl_path, include_paths, output_path,
         no_compile=False, no_source=False, vendor_stdlib=False,
         schema_override=None):
    """Create a .stapp package.

    Args:
        stl_path: path to the main STL file
        include_paths: list of include directories
        output_path: output .stapp file path
        no_compile: skip compilation (source-only package)
        no_source: strip STL source (STA-only package)
        vendor_stdlib: bundle stdlib files
    """
    import autocog
    from .program import _stdlib_path

    # Compile unless --no-compile
    sta_json = None
    if not no_compile:
        prog = autocog.compile(stl_path, includes=include_paths)
        sta_json = prog.sta
    else:
        # Still need to compile to get python_imports for file collection
        # Use a temp compile just for metadata
        prog = autocog.compile(stl_path, includes=include_paths)
        sta_json = None  # Don't include STA

    # Collect app files
    externals, stl_imports = _collect_app_files(prog, include_paths, stl_path)

    # Build manifest
    manifest = {
        "name": os.path.splitext(os.path.basename(stl_path))[0],
        "version": "1.0",
        "abi_version": sta_json["abi_version"] if sta_json else prog.sta["abi_version"],
        "entry_points": sta_json["entry_points"] if sta_json else prog.sta["entry_points"],
        "python_imports": sta_json["python_imports"] if sta_json else prog.sta["python_imports"],
        "vendor_stdlib": vendor_stdlib,
    }
    if sta_json:
        manifest["program"] = "program.sta.json"
    if not no_source:
        manifest["source"] = os.path.basename(stl_path)
    if stl_imports:
        manifest["imports"] = list(stl_imports.keys())
    if externals:
        manifest["externals"] = list(externals.keys())

    # Apply author schema overrides
    override_path = schema_override
    if override_path is None:
        # Auto-detect schema.json next to the STL file
        candidate = os.path.join(os.path.dirname(os.path.abspath(stl_path)), "schema.json")
        if os.path.isfile(candidate):
            override_path = candidate
    if override_path and os.path.isfile(override_path):
        import copy
        manifest["entry_points"] = copy.deepcopy(manifest["entry_points"])
        _merge_schema_overrides(manifest["entry_points"], override_path)

    # Create zip
    with zipfile.ZipFile(output_path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("manifest.json", json.dumps(manifest, indent=2))

        if sta_json:
            zf.writestr("program.sta.json", json.dumps(sta_json, indent=2))

        if not no_source:
            zf.write(stl_path, os.path.basename(stl_path))

        for fname, fpath in externals.items():
            zf.write(fpath, f"externals/{fname}")

        for fname, fpath in stl_imports.items():
            zf.write(fpath, f"stl/{fname}")

        if vendor_stdlib:
            stdlib = _stdlib_path()
            if stdlib:
                for fname in os.listdir(stdlib):
                    if fname.startswith("__"):
                        continue
                    fpath = os.path.join(stdlib, fname)
                    if os.path.isfile(fpath):
                        zf.write(fpath, f"stlib/{fname}")


def load_stapp(stapp_path, recompile=False):
    """Load a .stapp package.

    Extracts to a temp directory, reads manifest, loads or compiles program.

    Args:
        stapp_path: path to .stapp file
        recompile: force recompilation from source

    Returns:
        (program, manifest, temp_dir, include_paths)
        Caller should clean up temp_dir when done.
    """
    import autocog

    temp_dir = tempfile.mkdtemp(prefix="stapp_")

    with zipfile.ZipFile(stapp_path, "r") as zf:
        zf.extractall(temp_dir)

    manifest_path = os.path.join(temp_dir, "manifest.json")
    with open(manifest_path) as f:
        manifest = json.load(f)

    # Check ABI version compatibility
    import autocog
    installed = autocog.__version__
    required = manifest.get("abi_version", "")
    if required and installed != "unknown":
        def _parse_semver(v):
            parts = v.lstrip("v").split(".")
            return tuple(int(p) for p in parts[:3]) if len(parts) >= 3 else (0, 0, 0)
        inst_ver = _parse_semver(installed)
        req_ver = _parse_semver(required)
        if inst_ver[0] != req_ver[0]:
            shutil.rmtree(temp_dir, ignore_errors=True)
            raise ConfigError(
                f"ABI version mismatch: .stapp requires {required}, "
                f"installed autocog is {installed} (major version differs)"
            )
        if inst_ver[1] < req_ver[1]:
            import warnings
            warnings.warn(
                f"ABI version warning: .stapp built with {required}, "
                f"installed autocog is {installed} (minor version older)"
            )

    # Build include paths for externals and STL imports
    include_paths = []
    externals_dir = os.path.join(temp_dir, "externals")
    if os.path.isdir(externals_dir):
        include_paths.append(externals_dir)
    stl_dir = os.path.join(temp_dir, "stl")
    if os.path.isdir(stl_dir):
        include_paths.append(stl_dir)
    # Vendored stdlib
    stlib_dir = os.path.join(temp_dir, "stlib")
    if os.path.isdir(stlib_dir):
        include_paths.append(stlib_dir)

    # Load or compile
    sta_path = os.path.join(temp_dir, manifest.get("program", ""))
    source_name = manifest.get("source", "")
    source_path = os.path.join(temp_dir, source_name) if source_name else ""

    if not recompile and os.path.isfile(sta_path):
        prog = autocog.load(sta_path)
    elif os.path.isfile(source_path):
        prog = autocog.compile(source_path, includes=include_paths)
    else:
        raise ConfigError(
            f"Cannot load .stapp: no STA file and no source to compile"
        )

    return prog, manifest, temp_dir, include_paths
