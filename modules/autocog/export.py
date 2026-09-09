"""Export a harvest into a fine-tuning dataset: JSONL of token-id sequences
with per-token loss masks.

Each harvested step FTT is a single forced path; flattening it root-to-leaf
gives the exact token sequence the model should produce under the target
syntax. The loss-masking policy is deliberately a *parameter*, not a baked
decision — the first fine-tune ablates it:

    all        train on every token after the prompt (structure included —
               the policy that lets special tokens be internalized)
    value      train only on schema-field tokens (completions and choices);
               structural markers are context, never targets
    structure  train only on structural tokens (the special-token
               internalization probe: values as context)

The prompt (root node) is always context (mask 0). Rows::

    {"tokens": [...], "mask": [0/1...], "text": ...,
     "meta": {"syntax":..., "ctx":..., "prompt":..., "step":..., "model":...}}

Usage::

    python -m autocog.export --harvest DIR --out data.jsonl
        [--mask all|value|structure] [--syntaxes a,b]
"""

import argparse
import json
import os
import sys

EXPORT_FORMAT = "autocog-dataset"
EXPORT_VERSION = 1

POLICIES = ("all", "value", "structure")


def flatten(node, depth=0):
    """Yield (node, depth) along the single encoded path (first-child spine;
    harvested FTTs have exactly one path)."""
    yield node, depth
    children = node.get("children", [])
    if children:
        yield from flatten(children[0], depth + 1)


def step_to_row(ftt, policy, meta):
    tokens, mask, texts = [], [], []
    for node, depth in flatten(ftt):
        node_tokens = node.get("tokens", [])
        tokens.extend(node_tokens)
        texts.append(node.get("text", ""))
        if depth == 0:
            trainable = False  # the prompt is always context
        elif policy == "all":
            trainable = True
        elif policy == "value":
            trainable = node.get("field") is not None
        else:  # structure
            trainable = node.get("field") is None
        mask.extend([1 if trainable else 0] * len(node_tokens))
    return {"tokens": tokens, "mask": mask, "text": "".join(texts), "meta": meta}


def export(harvest_dir, out, policy="all", syntaxes=None):
    """Write one JSONL row per harvested (step, syntax). Returns row count."""
    if policy not in POLICIES:
        raise ValueError(f"unknown mask policy '{policy}' (one of {POLICIES})")
    manifest = json.load(open(os.path.join(harvest_dir, "harvest.json")))
    if manifest.get("format") != "autocog-harvest":
        raise ValueError(f"{harvest_dir} is not a harvest directory")

    rows = 0
    with open(out, "w") as f:
        header = {
            "format": EXPORT_FORMAT,
            "version": EXPORT_VERSION,
            "harvest": os.path.abspath(harvest_dir),
            "model": manifest["model"],
            "mask_policy": policy,
        }
        f.write(json.dumps(header) + "\n")
        for step in manifest["steps"]:
            if syntaxes and step["syntax"] not in syntaxes:
                continue
            ftt = json.load(open(os.path.join(harvest_dir, step["ftt"])))
            meta = {"syntax": step["syntax"], "ctx": step["ctx"],
                    "prompt": step["prompt"], "step": step["step"],
                    "model": manifest["model"], "stl": manifest["stl"]}
            f.write(json.dumps(step_to_row(ftt, policy, meta)) + "\n")
            rows += 1
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m autocog.export",
                                 description=__doc__.split("\n\n")[0])
    ap.add_argument("--harvest", required=True, help="harvest directory (see autocog.harvest)")
    ap.add_argument("--out", required=True, help="output JSONL path")
    ap.add_argument("--mask", default="all", choices=POLICIES,
                    help="loss-masking policy (default: all)")
    ap.add_argument("--syntaxes", help="restrict to these syntaxes (comma-separated)")
    args = ap.parse_args(argv)

    rows = export(args.harvest, args.out, policy=args.mask,
                  syntaxes=args.syntaxes.split(",") if args.syntaxes else None)
    print(f"exported {rows} examples (mask={args.mask}) -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
