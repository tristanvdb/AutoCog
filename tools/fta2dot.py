#!/usr/bin/env python3
"""Convert an FTA JSON file to Graphviz DOT format.

Usage:
    python fta2dot.py program.fta.json | dot -Tpng -o graph.png
    python fta2dot.py program.fta.json | dot -Tsvg -o graph.svg
"""

import json
import sys


def _esc(s):
    return s.replace("\\", "\\\\").replace('"', "'").replace("\n", "\\n")


def fta_to_dot(fta):
    actions = fta["actions"]

    # Build uid → index map
    uid_to_idx = {}
    for i, a in enumerate(actions):
        uid_to_idx[a["uid"]] = i

    lines = []
    lines.append("digraph FTA {")
    lines.append('    rankdir=TB;')
    lines.append('    node [fontname="monospace", fontsize=9];')
    lines.append('    edge [fontname="monospace", fontsize=8];')
    lines.append("")

    for i, a in enumerate(actions):
        uid = a["uid"]
        atype = a["type"]

        if atype == "text":
            text = a["text"]
            # Truncate long text
            if len(text) > 40:
                text = text[:37] + "..."
            text = _esc(text)
            label = f'{_esc(uid)}\\n{text}'
            color = "lightblue" if "field." in uid else "white"
            if "header" in uid:
                color = "lightyellow"
            elif "desc." in uid:
                color = "lemonchiffon"
            elif "var." in uid:
                color = "lightpink"
            elif "next." in uid:
                color = "lightcyan"
            lines.append(f'    n{i} [label="{label}", shape=box, style=filled, fillcolor="{color}"];')

        elif atype == "complete":
            length = a.get("length", "?")
            label = f'{_esc(uid)}\\ncomplete({length})'
            lines.append(f'    n{i} [label="{label}", shape=box, style="filled,rounded", fillcolor="lightgreen"];')

        elif atype == "choose":
            choices = a.get("choices", [])
            choices_str = " | ".join(str(c) for c in choices)
            if len(choices_str) > 50:
                choices_str = choices_str[:47] + "..."
            label = f'{_esc(uid)}\\n[{_esc(choices_str)}]'
            lines.append(f'    n{i} [label="{label}", shape=diamond, style=filled, fillcolor="lightsalmon"];')

    lines.append("")

    # Edges
    for i, a in enumerate(actions):
        succs = a.get("successors", [])
        for j, succ_uid in enumerate(succs):
            target = uid_to_idx.get(succ_uid)
            if target is not None:
                edge_label = ""
                if a["type"] == "choose" and j < len(a.get("choices", [])):
                    edge_label = f' [label="{_esc(str(a["choices"][j]))}"]'
                elif a["type"] == "choose":
                    edge_label = f' [label="succ[{j}]"]'
                lines.append(f'    n{i} -> n{target}{edge_label};')

    lines.append("}")
    return "\n".join(lines)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    with open(sys.argv[1]) as f:
        fta = json.load(f)

    print(fta_to_dot(fta))
