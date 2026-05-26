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


def main():
    args = parse_args()

    import autocog

    # Compile
    prog = autocog.compile(args.stl)

    # Create engine
    syntax = find_syntax(args)
    if args.rng:
        engine = autocog.Engine(syntax=syntax)
    else:
        engine = autocog.Engine(model=args.model, syntax=syntax, n_ctx=args.ctx)

    # Load input
    inputs = load_input(args.input)

    # Run
    result = engine.run(prog, entry=args.entry, **inputs)

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
