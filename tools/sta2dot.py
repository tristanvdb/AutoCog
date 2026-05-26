#!/usr/bin/env python3
"""Convert an STA JSON file to Graphviz DOT format (state graph).

Usage:
    python sta2dot.py program.sta.json [prompt_name] | dot -Tsvg -o graph.svg
"""

import json
import sys


def _esc(s):
    return s.replace("\\", "\\\\").replace('"', "'").replace("\n", "\\n")


def _node_id(tag):
    return "s_" + tag.replace("@", "__")


def sta_to_dot(sta, prompt_name=None):
    if prompt_name is None:
        entry = list(sta["entry_points"].values())[0]
        prompt_name = entry["prompt"] if isinstance(entry, dict) else entry

    prompt = None
    for p in sta["prompts"].values():
        if p["name"] == prompt_name:
            prompt = p
            break
    if prompt is None:
        prompt = sta["prompts"].get(prompt_name)
    if prompt is None:
        print(f"Prompt '{prompt_name}' not found", file=sys.stderr)
        sys.exit(1)

    states = prompt["states"]
    fields = prompt["fields"]
    flows = prompt.get("flows", {})

    lines = []
    lines.append("digraph STA {")
    lines.append('    rankdir=TB;')
    lines.append('    node [fontname="monospace", fontsize=10];')
    lines.append('    edge [fontname="monospace", fontsize=9];')
    lines.append("")

    for tag, state in states.items():
        nid = _node_id(tag)
        field_idx = state.get("field", -1)

        if field_idx < 0:
            # Root node
            lines.append(f'    {nid} [label="root", fillcolor="white", style="filled"];')
            continue

        f = fields[field_idx]
        fmt = f.get("format", {})
        fmt_type = fmt.get("type", "record") if isinstance(fmt, dict) else "record"
        rng = f.get("range")
        indices = state.get("indices", [])

        # Label: field path with indices
        idx_str = "".join(f"[{j}]" for j in indices)
        label = f"{f['name']}{idx_str}"

        # Color matching PoC: red=list tail, blue=record, orange=list element, gray=scalar
        is_list_tail = rng is not None and indices and indices[-1] >= rng[1]
        is_record = fmt_type == "record"
        is_list = rng is not None

        if is_list_tail:
            color = "red"
        elif is_record:
            color = "lightblue"
        elif is_list:
            color = "orange"
        else:
            color = "lightgray"

        lines.append(f'    {nid} [label="{_esc(label)}", fillcolor="{color}", style="filled"];')

    lines.append("")

    # Edges from successors (blue, constraint=true)
    for tag, state in states.items():
        src = _node_id(tag)
        for succ in state.get("successors", []):
            lines.append(f'    {src} -> {_node_id(succ)} [color=blue, constraint=true];')

    # Flow nodes
    for name, flow in flows.items():
        fid = _node_id("flow_" + name)
        lines.append(f'    {fid} [label="{_esc(name)} ({flow.get("type","?")})", shape=diamond, fillcolor="lightyellow", style="filled"];')
        for tag, state in states.items():
            if not state.get("successors"):
                lines.append(f'    {_node_id(tag)} -> {fid} [color=blue, constraint=true];')

    lines.append("}")
    return "\n".join(lines)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    with open(sys.argv[1]) as f:
        sta = json.load(f)

    prompt_name = sys.argv[2] if len(sys.argv) > 2 else None
    print(sta_to_dot(sta, prompt_name))
