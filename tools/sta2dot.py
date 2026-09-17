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

import html
import json
import sys

WIDTH = 46

COLOURS = {
    "root":       "#ffffff",
    "record":     "#e8f0fe",   # a nested structure
    "list":       "#ffe8cc",   # an array: the flow self-loop is the repeat
    "choice":     "#ffe0cc",   # the model picks from a fixed set
    "enum":       "#ffe0cc",
    "completion": "#f2f2f2",   # free generation
}


def _h(s):
    """Escape one line for an HTML-like label; never empty (graphviz rejects
    an empty element)."""
    return html.escape(str(s), quote=True) or "&nbsp;"


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


def _kind(f):
    fmt = f.get("format") or {}
    return fmt.get("type", "record") if isinstance(fmt, dict) else "record"


def _colour(f):
    if f.get("range"):
        return COLOURS["list"]
    return COLOURS.get(_kind(f), COLOURS["completion"])


def _detail(f):
    """The one line that says what this field asks the model for."""
    fmt = f.get("format") or {}
    kind = _kind(f)
    if kind == "completion":
        return f'text, up to {fmt.get("length", "?")} tokens'
    if kind == "choice":
        path = ".".join(st.get("name", "?") for st in fmt.get("path", []))
        return f'{fmt.get("mode", "choice")}({path})' if path else fmt.get("mode", "choice")
    if kind == "enum":
        vals = ", ".join(str(v) for v in fmt.get("values", []))
        return f'enum({vals})'
    return "record"


def _field_node(f):
    rows = [f'<TR><TD COLSPAN="2" ALIGN="LEFT"><B>{_h(f["name"])}</B>'
            f'</TD></TR>',
            f'<TR><TD COLSPAN="2" ALIGN="LEFT">'
            f'<FONT POINT-SIZE="9">{_h(_detail(f))}</FONT></TD></TR>']
    rng = f.get("range")
    if rng:
        rows.append('<TR><TD ALIGN="LEFT"><FONT POINT-SIZE="8" COLOR="#666666">'
                    f'repeats</FONT></TD><TD ALIGN="LEFT">'
                    f'<FONT POINT-SIZE="8">{rng[0]}..{rng[1]}</FONT></TD></TR>')
    for d in f.get("desc", [])[:2]:
        for line in [d[i:i + WIDTH] for i in range(0, len(d), WIDTH)][:3]:
            rows.append('<TR><TD COLSPAN="2" ALIGN="LEFT">'
                        f'<FONT POINT-SIZE="8" COLOR="#555555"><I>{_h(line)}'
                        f'</I></FONT></TD></TR>')
    return ('<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0" '
            f'BGCOLOR="{_colour(f)}">' + "".join(rows) + "</TABLE>")


def _channel_rows(channels):
    """Where each field's content comes from, if anywhere."""
    out = {}
    for ch in channels or []:
        target = ".".join(st.get("name", "?") for st in ch.get("target", []))
        source = ".".join(st.get("name", "?") for st in ch.get("source", []))
        kind = ch.get("type", "?")
        out.setdefault(target.split(".")[0], []).append(
            f'{kind}: {source or "-"}')
    return out


def sta_to_dot(sta, prompt_name=None):
    key, prompt = find_prompt(sta, prompt_name)
    abstracts = prompt["abstracts"]
    fields = prompt["fields"]
    flows = prompt.get("flows", {})

    channels = _channel_rows(prompt.get("channels"))

    out = [
        "digraph STA {",
        "    rankdir=TB;",
        '    graph [fontname="sans-serif", fontsize=10];',
        '    node [shape=plaintext, fontname="sans-serif"];',
        '    edge [fontname="sans-serif", fontsize=9];',
        f'    labelloc="t"; label="{_esc(key)}  —  schema, {len(fields)} fields";',
        "",
    ]

    for i, a in enumerate(abstracts):
        if a.get("field", -1) < 0:
            out.append(f'    s{i} [label="root", shape=circle, '
                       f'style=filled, fillcolor=white];')
        else:
            out.append(f'    s{i} [label=<{_field_node(fields[a["field"]])}>];')

    # Where a field's content arrives from, when it is not the model that
    # produces it. A field fed by a channel is fixed text at instantiation,
    # which is the difference between a prompt that asks and one that tells.
    for i, a in enumerate(abstracts):
        idx = a.get("field", -1)
        if idx < 0:
            continue
        rows = channels.get(fields[idx]["name"])
        if not rows:
            continue
        label = "<BR/>".join(_h(r) for r in rows)
        out.append(f'    ch{i} [label=<<FONT POINT-SIZE="8">{label}</FONT>>, '
                   f'shape=note, style=filled, fillcolor="#f6f6c8"];')
        out.append(f'    ch{i} -> s{i} [color="#999900", style=dotted, '
                   f'arrowhead=vee, constraint=false];')

    # The prompt's continuation: a return, or control passing to another
    # prompt. This is where a reader checks that a flow actually goes where
    # the source says it does.
    terminals = []
    for name, flow in flows.items():
        kind = flow.get("type", "?")
        target = flow.get("prompt")
        nid = "f_" + "".join(c if c.isalnum() or c == "_" else "_" for c in name)
        lines = [f"<B>{_h(name)}</B>", _h(f"({kind})")]
        if target:
            lines.insert(1, _h(f"\u2192 {target}"))
        out.append(f'    {nid} [label=<{"<BR/>".join(lines)}>, '
                   f'shape=note, style=filled, fillcolor="#fff3c4"];')
        terminals.append(nid)
    if not terminals:
        out.append('    f_end [label="end", shape=doublecircle, '
                   'style=filled, fillcolor=white];')
        terminals.append("f_end")

    out.append("")
    for i, a in enumerate(abstracts):
        flow, exit_ = a.get("flow", -1), a.get("exit", -1)
        if flow >= 0:
            style = "" if flow != i else ', label=" repeat"'
            out.append(f'    s{i} -> s{flow} [color="#1a73e8", penwidth=1.4{style}];')
        if exit_ > 0:
            out.append(f'    s{i} -> s{exit_} [color="#777777", style=dashed];')
        elif exit_ == 0 and i != 0:
            # Exit to the root is the end of the prompt, not a loop.
            for t in terminals:
                out.append(f'    s{i} -> {t} [color="#777777", style=dashed];')

    out.append("")
    out.append('    subgraph cluster_legend {')
    out.append('        label="legend"; fontsize=9; style=dotted; color="#bbbbbb";')
    out.append('        lg [label=<<TABLE BORDER="0" CELLBORDER="0" CELLSPACING="2">'
               '<TR><TD ALIGN="LEFT"><FONT COLOR="#1a73e8" POINT-SIZE="9">'
               '&#8594; flow</FONT></TD><TD ALIGN="LEFT"><FONT POINT-SIZE="9">'
               'enter a record, or repeat an array element</FONT></TD></TR>'
               '<TR><TD ALIGN="LEFT"><FONT COLOR="#777777" POINT-SIZE="9">'
               '&#8594; exit</FONT></TD><TD ALIGN="LEFT"><FONT POINT-SIZE="9">'
               'done with this field, on to the next</FONT></TD></TR>'
               '<TR><TD ALIGN="LEFT"><FONT COLOR="#999900" POINT-SIZE="9">'
               '&#8594; channel</FONT></TD><TD ALIGN="LEFT"><FONT POINT-SIZE="9">'
               'content supplied, not generated</FONT></TD></TR>'
               '</TABLE>>];')
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
