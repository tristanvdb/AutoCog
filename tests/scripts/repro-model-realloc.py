#!/usr/bin/env python3
"""Deterministic repro for the Model copy-relocation double-free.

The llama backend stores models in a `std::vector<Model>` (Manager::models).
`Model` has a user-declared destructor but no move constructor, so it is
silently copyable-but-not-movable; on vector growth the existing elements are
*copied*, the old element's destructor frees its llama_model*/llama_context*,
and the relocated copy is left aliasing freed handles.

This only bites once a *real* model is copy-relocated, which needs a SECOND real
model in the process:

  - Model #0 (RNG) has a NOP destructor and never frees anything.
  - create() #1 reallocs the vector but constructs #1 in place (no copy).
  - create() #2 reallocs again and copy-relocates #1 -> old #1's ~Model frees
    its context+model -> the surviving #1 dangles -> double free at teardown
    (or a CUDA device-free on a GPU build).

The normal test suite never loads two real models in one process, which is why
it passes with llama_free(ctx) re-enabled. This script does, so it crashes.

Usage:
  tests/scripts/repro-model-realloc.py [path/to/model.gguf]

Exit 0 (clean teardown) means the bug is FIXED; a crash/abort means it reproduces.
"""
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MODEL = REPO_ROOT / "models" / "tiny-llama3-test-Q2_K.gguf"


def main() -> int:
    model_path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_MODEL
    if not model_path.is_file():
        print(f"model not found: {model_path}\n"
              f"pass a GGUF path, or fetch the test model into {DEFAULT_MODEL.parent}/",
              file=sys.stderr)
        return 2

    from autocog.backend.llama import backend_llama_cxx as B

    n_ctx = int(os.environ.get("AUTOCOG_REPRO_NCTX", "256"))
    print(f"loading model #1 from {model_path} (n_ctx={n_ctx})", flush=True)
    m1 = B.create(str(model_path), n_ctx)
    print(f"  -> model #1 id={m1}", flush=True)

    # This second load grows Manager::models and copy-relocates model #1.
    print("loading model #2 (this triggers the vector realloc) ...", flush=True)
    m2 = B.create(str(model_path), n_ctx)
    print(f"  -> model #2 id={m2}", flush=True)

    # Touch the relocated first model: its handles were freed during the realloc
    # above, so this exercises the dangling pointer before teardown.
    print(f"querying model #1 vocab_size (uses the relocated handle) ...", flush=True)
    print(f"  -> vocab_size(#1)={B.vocab_size(m1)}", flush=True)

    print("both models created and used; exiting "
          "(watch for a double-free / device-free at teardown)", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
