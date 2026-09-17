#!/usr/bin/env python3
"""Convert an FTA to Graphviz DOT: the automaton a model will walk.

Usage:
    python fta2dot.py program.fta.json | dot -Tsvg -o graph.svg
    python fta2dot.py program.fta.json --width 64

Nodes are HTML tables rather than one-line labels, because the interesting
part of an action is its payload and a truncated label hides exactly that.
Each node shows its uid, the schema field it serves (if any), and its body:

    text       the literal rendering IN FULL, wrapped, newlines marked
    complete   the generation budget and the vocab/stop masks
    choose     one row per candidate, and the edge for candidate i LEAVES
               FROM THAT ROW -- successors are index-aligned with choices,
               so this is the one place where the alignment is visible
               rather than asserted.

Nothing is truncated. Pass `ista --simplify full` first unless you are
debugging the instantiator: it gathers the fixed text into as few actions as
the decisions allow (25 actions to 5 on the mcq demos), which is the
difference between a graph you read and a graph you scroll.
"""

import html
import json
import sys

WIDTH = 72          # characters per line inside a node, before wrapping

# Nothing is elided. The whole point of reading these is to see exactly what
# the model will be shown, and a graph that hides the middle of a prompt is
# worse than no graph -- it looks like it answered the question.

COLOURS = {
    "header": "#fff3c4",      # the system prompt and schema preview
    "desc": "#fdf6d8",        # field descriptions
    "branch": "#eaf3fb",      # the label that introduces a field
    "endl": "#f2f2f2",        # separators
    "field": "#dbeafe",       # a field's rendered value
    "text": "#ffffff",
    "complete": "#d9f2d9",    # free generation
    "choose": "#ffe0cc",      # constrained choice
}


def _cell(s):
    """Escape one line for an HTML-like label.

    Never returns empty: graphviz rejects an empty <FONT> element, and blank
    lines are common because trailing whitespace in a rendering is
    load-bearing -- it is what the model gets conditioned on."""
    return html.escape(s, quote=True) or "&nbsp;"


def _wrap(s, width):
    return [s[i:i + width] for i in range(0, len(s), width)] or [""]


def _text_rows(text, width):
    """Every line of a literal, wrapped, in full.

    A trailing pilcrow marks a real newline. Whitespace at the end of a
    rendering is load-bearing -- it is what the model gets conditioned on --
    and it is otherwise indistinguishable from the node's own line break."""
    rows = []
    paragraphs = text.split("\n")
    for i, para in enumerate(paragraphs):
        last_para = (i == len(paragraphs) - 1)
        if last_para and para == "" and len(paragraphs) > 1:
            break                      # the final newline is already marked
        chunks = _wrap(para, width)
        for j, chunk in enumerate(chunks):
            mark = "¶" if (not last_para and j == len(chunks) - 1) else ""
            rows.append(chunk + mark)
    return rows


def _kind_colour(uid, atype):
    if atype in ("complete", "choose"):
        return COLOURS[atype]
    for prefix, colour in COLOURS.items():
        if uid.startswith(prefix):
            return colour
    return COLOURS["text"]


def _head_rows(a, span):
    """uid, and the schema field this action serves."""
    rows = [f'<TR><TD COLSPAN="{span}" ALIGN="LEFT">'
            f'<FONT POINT-SIZE="9"><B>{_cell(a["uid"])}</B></FONT></TD></TR>']
    if a.get("field") is not None:
        idx = a.get("indices")
        where = f'field {a["field"]}' + (f' {idx}' if idx else "")
        rows.append(f'<TR><TD COLSPAN="{span}" ALIGN="LEFT">'
                    f'<FONT POINT-SIZE="8" COLOR="#666666">{_cell(where)}'
                    f'</FONT></TD></TR>')
    return rows


def _node(a, width, colour):
    atype = a["type"]
    rows = _head_rows(a, 1)

    if atype == "text":
        body = "".join(
            f'<TR><TD ALIGN="LEFT"><FONT FACE="monospace" POINT-SIZE="9">'
            f'{_cell(line)}</FONT></TD></TR>'
            for line in _text_rows(a.get("text", ""), width))
        rows.append(body)

    elif atype == "complete":
        bits = [f'complete, up to {a.get("length", "?")} tokens']
        if a.get("beams"):
            bits.append(f'beams={a["beams"]} ahead={a.get("ahead", 0)} '
                        f'width={a.get("width", 0)}')
        if a.get("vocab"):
            bits.append(f'vocab={a["vocab"]}')
        if a.get("stop"):
            bits.append(f'stop={a["stop"]}')
        rows.extend(f'<TR><TD ALIGN="LEFT"><FONT POINT-SIZE="9">{_cell(b)}'
                    f'</FONT></TD></TR>' for b in bits)

    return ('<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0" '
            f'BGCOLOR="{colour}">' + "".join(rows) + "</TABLE>")


def _choose_node(a, width, colour):
    """One row per candidate, each with a port so its edge starts there."""
    choices = a.get("choices", [])
    rows = _head_rows(a, 2)
    rows.append('<TR><TD COLSPAN="2" ALIGN="LEFT"><FONT POINT-SIZE="8" '
                f'COLOR="#666666">choose 1 of {len(choices)}</FONT></TD></TR>')

    for i, c in enumerate(choices):
        text = "<BR ALIGN='LEFT'/>".join(
            _cell(line) for line in _text_rows(str(c), width - 6))
        rows.append(
            f'<TR><TD PORT="c{i}" WIDTH="22"><FONT POINT-SIZE="8">{i}</FONT></TD>'
            f'<TD ALIGN="LEFT"><FONT FACE="monospace" POINT-SIZE="9">'
            f'{text}</FONT></TD></TR>')
    return ('<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0" '
            f'BGCOLOR="{colour}">' + "".join(rows) + "</TABLE>")


def fta_to_dot(fta, width=WIDTH):
    actions = fta["actions"]
    uid_to_idx = {a["uid"]: i for i, a in enumerate(actions)}

    title = "FTA"
    if fta.get("simplified"):
        title += f' ({fta["simplified"]}-simplified)'
    title += f" — {len(actions)} actions"

    out = [
        "digraph FTA {",
        "    rankdir=TB;",
        '    graph [fontname="sans-serif", fontsize=10];',
        '    node [shape=plaintext, fontname="sans-serif"];',
        '    edge [fontname="sans-serif", fontsize=8, color="#555555"];',
        f'    labelloc="t"; label="{html.escape(title)}";',
        "",
    ]

    for i, a in enumerate(actions):
        colour = _kind_colour(a["uid"], a["type"])
        label = (_choose_node(a, width, colour) if a["type"] == "choose"
                 else _node(a, width, colour))
        out.append(f"    n{i} [label=<{label}>];")

    out.append("")
    for i, a in enumerate(actions):
        succs = a.get("successors", [])
        for j, succ_uid in enumerate(succs):
            target = uid_to_idx.get(succ_uid)
            if target is None:
                out.append(f'    n{i} -> dangling_{i}_{j} '
                           f'[color=red, label="missing"];')
                out.append(f'    dangling_{i}_{j} [label=<<B>{html.escape(succ_uid)}'
                           f'</B><BR/>not in this FTA>, fontcolor=red];')
                continue
            if a["type"] == "choose" and j < len(a.get("choices", [])):
                # Leave from the candidate's own row: successors are
                # index-aligned with choices, and this is where that shows.
                out.append(f"    n{i}:c{j}:e -> n{target};")
            else:
                out.append(f"    n{i} -> n{target};")

    out.append("}")
    return "\n".join(out)


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    width = WIDTH
    if "--width" in sys.argv:
        width = int(sys.argv[sys.argv.index("--width") + 1])

    with open(args[0]) as f:
        fta = json.load(f)

    print(fta_to_dot(fta, width))
