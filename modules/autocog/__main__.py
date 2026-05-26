"""
AutoCog CLI — compile and run STL programs.

Usage:
    python -m autocog --stl program.stl --model model.gguf --syntax syntax.json --input data.json
    python -m autocog --stl program.stl --rng --input '{"topic":"Science"}'
"""

import argparse
import json
import os
import sys


def parse_args():
    parser = argparse.ArgumentParser(
        prog="python -m autocog",
        description="Compile and run STL programs with AutoCog.",
    )

    # Program
    parser.add_argument(
        "--stl", required=True,
        help="STL program file to compile and run",
    )
    parser.add_argument(
        "--entry", default="main",
        help="Entry point prompt name (default: main)",
    )
    parser.add_argument(
        "-I", "--include", action="append", default=[],
        help="Import search paths (can be repeated)",
    )

    # Model (exactly one of --model or --rng required)
    model_group = parser.add_mutually_exclusive_group(required=True)
    model_group.add_argument(
        "--model",
        help="Path to GGUF model file",
    )
    model_group.add_argument(
        "--rng", action="store_true",
        help="Use built-in RNG model (testing only)",
    )

    # Syntax
    parser.add_argument(
        "--syntax", default=None,
        help="Syntax description JSON file (default: share/syntax/default.json)",
    )

    # Input
    parser.add_argument(
        "--input", default=None,
        help="Input data as JSON file path or inline JSON string",
    )

    # Output
    parser.add_argument(
        "-o", "--output", default=None,
        help="Write result to file (default: stdout)",
    )

    # Context
    parser.add_argument(
        "--ctx", type=int, default=4096,
        help="Model context size (default: 4096)",
    )

    # Execution
    parser.add_argument(
        "-v", "--verbose", action="store_true",
        help="Show step-by-step execution progress",
    )
    parser.add_argument(
        "--max-steps", type=int, default=100,
        help="Maximum number of prompt steps (default: 100)",
    )

    return parser.parse_args()


def find_syntax(args):
    """Resolve syntax path, trying defaults if not specified."""
    if args.syntax:
        return args.syntax

    # Look relative to the repo
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(here, "..", "..", "share", "syntax", "default.json"),
        os.path.join(os.getcwd(), "share", "syntax", "default.json"),
    ]
    for c in candidates:
        if os.path.isfile(c):
            return os.path.abspath(c)

    print("Error: no syntax file found. Use --syntax to specify one.", file=sys.stderr)
    sys.exit(1)


def load_input(input_arg):
    """Parse input as JSON file or inline JSON string."""
    if input_arg is None:
        return {}

    # Try as file first
    if os.path.isfile(input_arg):
        with open(input_arg) as f:
            return json.load(f)

    # Try as inline JSON
    try:
        return json.loads(input_arg)
    except json.JSONDecodeError as e:
        print(f"Error: --input is not a valid file path or JSON string: {e}", file=sys.stderr)
        sys.exit(1)


def load_externals(program, include_paths):
    """Auto-load Python externals referenced by the program.

    Scans python_imports in the STA, finds the .py files in include paths,
    and loads the functions.
    """
    import importlib.util

    externals = {}
    loaded_modules = {}

    for extern_name, info in program.python_imports.items():
        py_file = info["file"]
        target_func = info["target"]

        # Find the .py file in include paths
        module_path = None
        for inc in include_paths:
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


def main():
    args = parse_args()

    import autocog

    # Compile
    prog = autocog.compile(args.stl, includes=args.include)

    # Create engine
    syntax = find_syntax(args)
    if args.rng:
        engine = autocog.Engine(syntax=syntax)
    else:
        engine = autocog.Engine(model=args.model, syntax=syntax, n_ctx=args.ctx)

    # Load input
    inputs = load_input(args.input)

    # Auto-load Python externals
    externals = load_externals(prog, args.include)

    # Run
    if args.verbose:
        import time
        from autocog.context import Context
        prompt = prog.entry_points.get(args.entry)
        if prompt is None:
            print(f"Error: entry point '{args.entry}' not found", file=sys.stderr)
            sys.exit(1)
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
    if isinstance(result, dict):
        output = json.dumps(result, indent=2, ensure_ascii=False)
    elif isinstance(result, list):
        output = json.dumps(result, indent=2, ensure_ascii=False)
    else:
        output = str(result)

    if args.output:
        with open(args.output, "w") as f:
            f.write(output + "\n")
    else:
        print(output)


if __name__ == "__main__":
    main()
