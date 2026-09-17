#!/usr/bin/env python3
"""Convert an STA prompt to Graphviz DOT: the schema's control structure.

Usage:
    python sta2dot.py program.sta.json [prompt_name] | dot -Tsvg -o graph.svg
    python sta2dot.py program.sta.json --list

This draws the ABSTRACT state graph -- one node per schema field, the size of
the source. It is not the automaton the model walks: arrays are still rolled
up here, and the instantiator unrolls them against the content it is given
(that unrolled form is the FTA, see fta2dot.py). Read this one to check that
a program says what you meant; read the FTA to check what a model will see.

Each state carries two out-edges, either of which may be absent:

    flow   enter this field -- descend into a record, or take one more
           element of an array. A self-loop is an array repeating.
    exit   leave this field for the next one. An exit back to the root is
           the end of the prompt, drawn here as a terminal rather than as a
           back-edge, because a cycle through the root reads as a loop and
           it is not one.

Whether an edge is available at runtime depends on the array bounds, which
the instantiator resolves per element: `can_flow = count < max`,
`can_exit = count >= min` (runtime/sta/instantiate.cxx). The bounds are on
the node so the two can be read together.
"""

import json
import sys


def _esc(s):
    """Escape one piece of label text. Never call this on a string that already
    contains a DOT line break -- it would escape the backslash and the break
    would render as a literal \\n, which is exactly what it used to do."""
    return s.replace("\\", "\\\\").replace('"', "'").replace("\n", " ")


def _label(*lines):
    """Escape each line, then join with the DOT line break."""
    return "\\n".join(_esc(str(l)) for l in lines if l is not None and l != "")


def find_prompt(sta, prompt_name=None):
    """Prompts are stored under their mangled name (`choose__8`): a prompt
    with arguments is a distinct prompt per instantiation. Accept either the
    mangled name or the one written in the source."""
    prompts = sta["prompts"]
    if prompt_name is None:
        entry = list(sta["entry_points"].values())[0]
        prompt_name = entry["prompt"] if isinstance(entry, dict) else entry
    if prompt_name in prompts:
        return prompt_name, prompts[prompt_name]
    for key, p in prompts.items():
        if p.get("name") == prompt_name:
            return key, p
    for key, p in prompts.items():
        if key.split("__")[0] == prompt_name:
            return key, p
    print(f"Prompt '{prompt_name}' not found (have: {', '.join(prompts)})",
          file=sys.stderr)
    sys.exit(1)


def _field_label(fields, idx):
    """The lines of a field's node label."""
    f = fields[idx]
    fmt = f.get("format") or {}
    kind = fmt.get("type", "record") if isinstance(fmt, dict) else "record"
    detail = kind
    rng = f.get("range")
    if rng:
        detail += f" [{rng[0]}:{rng[1]}]"
    if kind == "completion" and fmt.get("length"):
        detail += f" len={fmt['length']}"
    if kind == "choice" and fmt.get("mode"):
        detail += f" ({fmt['mode']})"
    lines = [f["name"], detail]
    if kind == "enum" and fmt.get("values"):
        vals = ",".join(str(v) for v in fmt["values"])
        lines.append(vals[:40])
    return lines


def _colour(fields, idx):
    f = fields[idx]
    fmt = f.get("format") or {}
    kind = fmt.get("type", "record") if isinstance(fmt, dict) else "record"
    if f.get("range"):
        return "orange"          # an array: the flow self-loop is the repeat
    if kind == "record":
        return "lightblue"
    if kind in ("choice", "enum"):
        return "palegreen"       # the model picks from a fixed set
    return "lightgray"           # free generation


def sta_to_dot(sta, prompt_name=None):
    key, prompt = find_prompt(sta, prompt_name)
    abstracts = prompt["abstracts"]
    fields = prompt["fields"]
    flows = prompt.get("flows", {})

    out = [
        "digraph STA {",
        "    rankdir=TB;",
        '    node [fontname="monospace", fontsize=10, shape=box, style=filled];',
        '    edge [fontname="monospace", fontsize=9];',
        f'    labelloc="t"; label="{_esc(key)}  (abstract states)";',
        "",
    ]

    for i, a in enumerate(abstracts):
        if a.get("field", -1) < 0:
            out.append(f'    s{i} [label="root", shape=circle, fillcolor=white];')
        else:
            out.append(f'    s{i} [label="{_label(*_field_label(fields, a["field"]))}", '
                       f'fillcolor={_colour(fields, a["field"])}];')

    # The prompt's continuation: a return, or control passing to another
    # prompt. This is where a reader checks that a flow actually goes where
    # the source says it does.
    terminals = []
    for name, flow in flows.items():
        kind = flow.get("type", "?")
        target = flow.get("prompt")
        nid = "f_" + "".join(c if c.isalnum() or c == "_" else "_" for c in name)
        lines = [name] + ([f"-> {target}"] if target else []) + [f"({kind})"]
        out.append(f'    {nid} [label="{_label(*lines)}", '
                   f'shape=diamond, fillcolor=lightyellow];')
        terminals.append(nid)
    if not terminals:
        out.append('    f_end [label="end", shape=doublecircle, fillcolor=white];')
        terminals.append("f_end")

    out.append("")
    for i, a in enumerate(abstracts):
        flow, exit_ = a.get("flow", -1), a.get("exit", -1)
        if flow >= 0:
            style = "" if flow != i else ', label="repeat"'
            out.append(f'    s{i} -> s{flow} [color=blue{style}];')
        if exit_ > 0:
            out.append(f'    s{i} -> s{exit_} [color=gray40, style=dashed];')
        elif exit_ == 0 and i != 0:
            # Exit to the root is the end of the prompt, not a loop.
            for t in terminals:
                out.append(f'    s{i} -> {t} [color=gray40, style=dashed];')

    out.append("")
    out.append('    subgraph cluster_legend {')
    out.append('        label="edges"; fontsize=9; style=dotted;')
    out.append('        l0 [label="flow: enter / repeat", shape=plaintext, fillcolor=white];')
    out.append('        l1 [label="exit: next field", shape=plaintext, fillcolor=white];')
    out.append('        l0 -> l1 [style=invis];')
    out.append('    }')
    out.append("}")
    return "\n".join(out)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    with open(sys.argv[1]) as f:
        sta = json.load(f)

    if "--list" in sys.argv[2:]:
        for key, p in sta["prompts"].items():
            print(key)
        sys.exit(0)

    prompt_name = sys.argv[2] if len(sys.argv) > 2 else None
    print(sta_to_dot(sta, prompt_name))
