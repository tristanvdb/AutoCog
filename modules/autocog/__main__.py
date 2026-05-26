"""
AutoCog CLI — compile and run STL programs.

Usage:
    autocog compile --stl program.stl -o program.sta.json
    autocog run --stl program.stl --rng --input data.json
    autocog run --sta program.sta.json --model model.gguf --input data.json
"""

import argparse
import json
import os
import sys


def load_externals(program, include_paths):
    """Auto-load Python externals referenced by the program.

    Scans python_imports in the STA, finds the .py files in include paths
    (and the stdlib), and loads the functions.
    """
    import importlib.util
    from .program import _stdlib_path

    # Build search paths: explicit includes, then stdlib
    search_paths = list(include_paths)
    stdlib = _stdlib_path()
    if stdlib and stdlib not in search_paths:
        search_paths.append(stdlib)

    externals = {}
    loaded_modules = {}

    for extern_name, info in program.python_imports.items():
        py_file = info["file"]
        target_func = info["target"]

        # Find the .py file in search paths
        module_path = None
        for inc in search_paths:
            candidate = os.path.join(inc, py_file)
            if os.path.isfile(candidate):
                module_path = candidate
                break

        if module_path is None:
            print(f"Warning: Python file '{py_file}' not found in include paths", file=sys.stderr)
            continue

        # Load the module (cache by path)
        if module_path not in loaded_modules:
            mod_name = os.path.splitext(os.path.basename(py_file))[0]
            spec = importlib.util.spec_from_file_location(mod_name, module_path)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            loaded_modules[module_path] = mod

        mod = loaded_modules[module_path]
        func = getattr(mod, target_func, None)
        if func is None:
            print(f"Warning: function '{target_func}' not found in '{py_file}'", file=sys.stderr)
            continue

        externals[extern_name] = func

    return externals


def _load_program(args):
    """Load program from --stl, --sta, or --app.

    Returns (program, include_paths, cleanup_fn).
    cleanup_fn should be called when done (for .stapp temp dirs).
    """
    import autocog
    if hasattr(args, 'stl') and args.stl:
        return autocog.compile(args.stl, includes=args.include), args.include, None
    elif hasattr(args, 'sta') and args.sta:
        return autocog.load(args.sta), args.include, None
    elif hasattr(args, 'app') and args.app:
        from .stapp import load_stapp
        recompile = getattr(args, 'recompile', False)
        prog, manifest, temp_dir, inc_paths = load_stapp(args.app, recompile=recompile)
        import shutil
        return prog, inc_paths + args.include, lambda: shutil.rmtree(temp_dir, ignore_errors=True)
    else:
        print("Error: --stl, --sta, or --app required", file=sys.stderr)
        sys.exit(1)


def cmd_pack(args):
    """Pack an STL program into a .stapp."""
    from .stapp import pack
    pack(
        stl_path=args.stl,
        include_paths=args.include,
        output_path=args.output,
        no_compile=args.no_compile,
        no_source=args.no_source,
        vendor_stdlib=args.vendor_stdlib,
        schema_override=args.schema,
    )


def cmd_backend(args):
    """Start level-3 backend server."""
    import uvicorn
    from .server.backend import create_app

    model_path = args.model if not args.rng else None
    app = create_app(model_path=model_path, n_ctx=args.ctx)
    uvicorn.run(app, host=args.host, port=args.port)


def cmd_rpc(args):
    """Start level-2 RPC server."""
    import uvicorn
    from .server.rpc import create_app

    prog, include_paths, cleanup = _load_program(args)
    model_path = args.model if not args.rng else None
    app = create_app(
        program=prog, model_path=model_path,
        syntax_path=args.syntax, n_ctx=args.ctx
    )
    try:
        uvicorn.run(app, host=args.host, port=args.port)
    finally:
        if cleanup:
            cleanup()


def cmd_serve(args):
    """Start level-1 full app server."""
    import uvicorn
    from .server.serve import create_app

    prog, include_paths, cleanup = _load_program(args)
    externals = load_externals(prog, include_paths)
    model_path = args.model if not args.rng else None
    app = create_app(
        program=prog, externals=externals,
        model_path=model_path, syntax_path=args.syntax, n_ctx=args.ctx
    )
    try:
        uvicorn.run(app, host=args.host, port=args.port)
    finally:
        if cleanup:
            cleanup()


def _add_program_args(parser):
    """Add --stl, --sta, --app, -I to a subparser."""
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--stl", help="STL source file to compile")
    group.add_argument("--sta", help="Pre-compiled STA JSON file")
    group.add_argument("--app", help=".stapp package file")
    parser.add_argument(
        "-I", "--include", action="append", default=[],
        help="Import search path (repeatable)",
    )
    parser.add_argument("--recompile", action="store_true",
        help="Force recompilation from source (.stapp only)")


def cmd_compile(args):
    """Compile STL to STA."""
    import autocog
    prog = autocog.compile(args.stl, includes=args.include)
    output = json.dumps(prog.sta, indent=2)
    if args.output:
        with open(args.output, "w") as f:
            f.write(output + "\n")
    else:
        print(output)


def cmd_run(args):
    """Run a program."""
    import autocog

    prog, include_paths, cleanup = _load_program(args)
    try:
        _cmd_run_inner(args, prog, include_paths)
    finally:
        if cleanup:
            cleanup()


def _cmd_run_inner(args, prog, include_paths):
    """Inner run logic (separated for cleanup)."""
    import autocog

    # Parse inputs
    inputs = {}
    if args.input:
        if os.path.isfile(args.input):
            with open(args.input) as f:
                inputs = json.load(f)
        else:
            inputs = json.loads(args.input)

    # Create engine
    if args.model:
        engine = autocog.Engine(model=args.model, syntax=args.syntax, n_ctx=args.ctx)
    elif args.rng:
        engine = autocog.Engine(syntax=args.syntax)
    else:
        print("Error: --model or --rng required", file=sys.stderr)
        sys.exit(1)

    # Auto-load Python externals
    externals = load_externals(prog, include_paths)

    # Run
    if args.verbose:
        import time
        from autocog.context import Context
        prompt = prog.entry_prompt(args.entry)
        ctx = Context(prog, engine, prompt, inputs, externals)
        steps = 0
        t0 = time.time()
        while not ctx.done and steps < args.max_steps:
            t1 = time.time()
            ctx.step()
            steps += 1
            elapsed = time.time() - t1
            print(f"  [{steps}] {ctx.prompt} ({elapsed:.2f}s)", file=sys.stderr)
        if not ctx.done:
            print(f"Warning: did not complete after {steps} steps", file=sys.stderr)
        total = time.time() - t0
        print(f"  Done in {steps} steps ({total:.1f}s)", file=sys.stderr)
        result = ctx.result if ctx.done else ctx.frames.get(ctx.prompt, {})
    else:
        result = engine.run(prog, entry=args.entry, externals=externals,
                            max_steps=args.max_steps, **inputs)

    # Output
    if isinstance(result, str):
        output = result
    elif isinstance(result, dict):
        output = json.dumps(result, indent=2, default=str)
    else:
        output = str(result)

    if args.output:
        with open(args.output, "w") as f:
            f.write(output + "\n")
    else:
        print(output)


def main():
    parser = argparse.ArgumentParser(
        prog="autocog",
        description="AutoCog — Structured Thought Language tools",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # --- compile ---
    p_compile = subparsers.add_parser("compile", help="Compile STL to STA")
    p_compile.add_argument("--stl", required=True, help="STL source file")
    p_compile.add_argument(
        "-I", "--include", action="append", default=[],
        help="Import search path (repeatable)",
    )
    p_compile.add_argument("-o", "--output", help="Output file (default: stdout)")

    # --- pack ---
    p_pack = subparsers.add_parser("pack", help="Pack STL into a .stapp")
    p_pack.add_argument("--stl", required=True, help="STL source file")
    p_pack.add_argument(
        "-I", "--include", action="append", default=[],
        help="Import search path (repeatable)",
    )
    p_pack.add_argument("-o", "--output", required=True, help="Output .stapp file")
    p_pack.add_argument("--no-compile", action="store_true",
        help="Include source only, no pre-compiled STA")
    p_pack.add_argument("--no-source", action="store_true",
        help="Strip STL source (STA only, IP protection)")
    p_pack.add_argument("--vendor-stdlib", action="store_true",
        help="Bundle stdlib files into the package")
    p_pack.add_argument("--schema", default=None,
        help="Schema override JSON file (default: auto-detect schema.json)")

    # --- run ---
    p_run = subparsers.add_parser("run", help="Run a program")
    _add_program_args(p_run)
    p_run.add_argument("--input", help="Input data (file path or inline JSON)")
    p_run.add_argument("--model", help="GGUF model file")
    p_run.add_argument("--rng", action="store_true", help="Use built-in RNG model")
    p_run.add_argument(
        "--syntax", default=None,
        help="Syntax description JSON (default: built-in)",
    )
    p_run.add_argument("--entry", default="main", help="Entry point (default: main)")
    p_run.add_argument("--ctx", type=int, default=4096, help="Model context size")
    p_run.add_argument("-o", "--output", help="Output file (default: stdout)")
    p_run.add_argument("-v", "--verbose", action="store_true", help="Show step progress")
    p_run.add_argument("--max-steps", type=int, default=100, help="Max prompt steps")

    # --- Server common args ---
    def _add_server_args(p):
        p.add_argument("--model", help="GGUF model file")
        p.add_argument("--rng", action="store_true", help="Use built-in RNG model")
        p.add_argument("--ctx", type=int, default=4096, help="Model context size")
        p.add_argument("--host", default="0.0.0.0", help="Bind host (default: 0.0.0.0)")
        p.add_argument("--port", type=int, default=8080, help="Bind port (default: 8080)")

    # --- backend (level 3) ---
    p_backend = subparsers.add_parser("backend", help="Serve FTA evaluation (level 3)")
    _add_server_args(p_backend)

    # --- rpc (level 2) ---
    p_rpc = subparsers.add_parser("rpc", help="Serve prompt evaluation (level 2)")
    _add_program_args(p_rpc)
    _add_server_args(p_rpc)
    p_rpc.add_argument("--syntax", default=None, help="Syntax description JSON")

    # --- serve (level 1) ---
    p_serve = subparsers.add_parser("serve", help="Serve full app with web UI (level 1)")
    _add_program_args(p_serve)
    _add_server_args(p_serve)
    p_serve.add_argument("--syntax", default=None, help="Syntax description JSON")

    args = parser.parse_args()

    # Default syntax (for run, rpc, serve)
    if args.command in ("run", "rpc", "serve") and getattr(args, 'syntax', None) is None:
        from .program import _default_syntax_path
        default = _default_syntax_path()
        if default:
            args.syntax = default
        else:
            print("Error: --syntax required (could not find default)", file=sys.stderr)
            sys.exit(1)

    if args.command == "compile":
        cmd_compile(args)
    elif args.command == "pack":
        cmd_pack(args)
    elif args.command == "run":
        cmd_run(args)
    elif args.command == "backend":
        cmd_backend(args)
    elif args.command == "rpc":
        cmd_rpc(args)
    elif args.command == "serve":
        cmd_serve(args)


if __name__ == "__main__":
    main()
