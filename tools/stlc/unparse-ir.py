#!/usr/bin/env python3
"""
Unparse IR JSON back to STL source code.

Produces fully-instantiated STL prompts with all templates expanded,
records inlined, and parameters resolved — analogous to viewing
preprocessed C++ after template instantiation.

Usage:
    stlc --ir /dev/stdout program.stl | python tools/stlc/unparse-ir.py
    python tools/stlc/unparse-ir.py < ir.json
    python tools/stlc/unparse-ir.py ir.json
"""

import json
import sys


# ============================================================================
# Path / range formatting
# ============================================================================

def fmt_range(r):
    """Format a range: [4] or [1:10] or empty string."""
    if r is None:
        return ""
    if len(r) == 1:
        return f"[{r[0]}]"
    return f"[{r[0]}:{r[1]}]"


def fmt_path(steps):
    """Format a list of path steps: a.b[1:3].c"""
    parts = []
    for s in steps:
        parts.append(s["name"] + fmt_range(s.get("range")))
    return ".".join(parts)


def fmt_docpath(dp):
    """Format a DocPath, optionally prefixed by prompt name."""
    path = fmt_path(dp["steps"])
    if dp.get("prompt"):
        return f"{dp['prompt']}.{path}"
    return path


# ============================================================================
# Format types
# ============================================================================

def fmt_format(f):
    """Convert a format dict to STL type syntax."""
    t = f["type"]

    if t == "completion":
        kwargs = []
        for k in ("length", "threshold", "beams", "ahead", "width", "within"):
            if k in f and f[k] is not None:
                kwargs.append(f"{k}={f[k]}")
        if kwargs:
            return f"text<{', '.join(kwargs)}>"
        return "text"

    if t == "enum":
        vals = ", ".join(fmt_string(v.strip('"')) for v in f["values"])
        s = f"enum({vals})"
        if f.get("width") is not None:
            s += f"<width={f['width']}>"
        return s

    if t == "choice":
        mode = f["mode"]  # "select" or "repeat"
        path = fmt_docpath(f["path"])
        s = f"{mode}({path})"
        if f.get("width") is not None:
            s += f"<width={f['width']}>"
        return s

    if t == "record":
        return None  # handled inline as a nested struct

    return f"/* unknown format: {t} */"


# ============================================================================
# Fields
# ============================================================================

def emit_fields(fields, indent, lines):
    """Emit a list of fields as an `is { ... }` block."""
    if not fields:
        return

    lines.append(f"{indent}is {{")
    for f in fields:
        name = f["name"]
        rng = fmt_range(f.get("range"))
        fmt = f.get("format")

        # Field-level annotations
        for d in f.get("desc", []):
            lines.append(f'{indent}  annotate {name} {fmt_string(d)};')

        if fmt is None:
            lines.append(f"{indent}  {name}{rng} is text;")
        elif fmt["type"] == "record":
            # Inline nested struct
            sub_fields = fmt.get("fields", [])
            if len(sub_fields) == 1 and sub_fields[0].get("format"):
                # Single-field record: inline the format
                sf = sub_fields[0]
                sf_fmt = fmt_format(sf["format"])
                desc = fmt.get("desc", [])
                if sf_fmt:
                    lines.append(f"{indent}  {name}{rng} is {sf_fmt};")
                else:
                    lines.append(f"{indent}  {name}{rng} is text;")
                for d in desc:
                    lines.append(f'{indent}  annotate {name} {fmt_string(d)};')
            else:
                lines.append(f"{indent}  {name}{rng} is {{")
                for sf in sub_fields:
                    sf_name = sf["name"]
                    sf_rng = fmt_range(sf.get("range"))
                    sf_fmt_obj = sf.get("format")
                    if sf_fmt_obj and sf_fmt_obj["type"] != "record":
                        sf_fmt = fmt_format(sf_fmt_obj)
                        lines.append(f"{indent}    {sf_name}{sf_rng} is {sf_fmt};")
                    elif sf_fmt_obj and sf_fmt_obj["type"] == "record":
                        # Deeper nesting — recurse
                        lines.append(f"{indent}    {sf_name}{sf_rng} is {{")
                        for ssf in sf_fmt_obj.get("fields", []):
                            ssf_name = ssf["name"]
                            ssf_rng = fmt_range(ssf.get("range"))
                            ssf_fmt = fmt_format(ssf.get("format", {})) or "text"
                            lines.append(f"{indent}      {ssf_name}{ssf_rng} is {ssf_fmt};")
                        lines.append(f"{indent}    }}")
                    else:
                        lines.append(f"{indent}    {sf_name}{sf_rng} is text;")
                lines.append(f"{indent}  }}")
                for d in fmt.get("desc", []):
                    lines.append(f'{indent}  annotate {name} {fmt_string(d)};')
        else:
            type_str = fmt_format(fmt)
            lines.append(f"{indent}  {name}{rng} is {type_str};")

    lines.append(f"{indent}}}")


# ============================================================================
# Channels
# ============================================================================

def emit_channels(channels, indent, lines):
    """Emit a channel block."""
    if not channels:
        return

    lines.append(f"{indent}channel {{")
    for ch in channels:
        target = fmt_docpath(ch["target"])
        t = ch["type"]

        if t == "input":
            source = fmt_path(ch["source"])
            lines.append(f"{indent}  {target} get {source};")

        elif t == "dataflow":
            source = fmt_path(ch["source"])
            if ch.get("prompt"):
                source = f"{ch['prompt']}.{source}"
            lines.append(f"{indent}  {target} use {source};")

        elif t == "call":
            entry = ch.get("entry", "???")
            kwargs_parts = []
            for kname, kwarg in ch.get("kwargs", {}).items():
                src_path = fmt_path(kwarg["path"])
                if kwarg.get("prompt"):
                    src_path = f"{kwarg['prompt']}.{src_path}"
                if kwarg.get("is_input"):
                    kwargs_parts.append(f"{kname} get {src_path}")
                else:
                    kwargs_parts.append(f"{kname} use {src_path}")
            if kwargs_parts:
                kwargs_str = ", ".join(kwargs_parts)
                lines.append(f"{indent}  {target} call {entry}({kwargs_str});")
            else:
                lines.append(f"{indent}  {target} call {entry};")

    lines.append(f"{indent}}}")


# ============================================================================
# Flows
# ============================================================================

def emit_flows(flows, indent, lines):
    """Emit flow statements."""
    for flow in flows:
        target = flow["target"]
        parts = []
        if flow.get("limit") is not None:
            parts.append(f"limit={flow['limit']}")
        if flow.get("label") is not None:
            parts.append(f'label="{flow["label"]}"')
        if parts:
            lines.append(f"{indent}flow {target}<{', '.join(parts)}>;")
        else:
            lines.append(f"{indent}flow {target};")


# ============================================================================
# Return
# ============================================================================

def fmt_string(s):
    """Format a string literal with escaping."""
    escaped = s.replace('\\', '\\\\').replace('"', '\\"')
    return f'"{escaped}"'


def emit_return(ret, indent, lines):
    """Emit a return block."""
    if ret is None:
        return

    label_part = ""
    if ret.get("label"):
        label_part = f' {fmt_string(ret["label"])}'

    lines.append(f"{indent}return{label_part} {{")
    for rf in ret.get("fields", []):
        alias = rf.get("alias")
        constant = rf.get("constant")

        if constant is not None:
            # Constant value: `alias is "value";`
            if alias:
                lines.append(f'{indent}  {alias} is {fmt_string(constant)};')
            else:
                lines.append(f'{indent}  is {fmt_string(constant)};')
        else:
            path = fmt_docpath(rf["source"])
            if alias:
                lines.append(f"{indent}  {alias} use {path};")
            else:
                lines.append(f"{indent}  use {path};")

    lines.append(f"{indent}}}")


# ============================================================================
# Top-level: prompt and record unparsing
# ============================================================================

def unparse_prompt(name, prompt):
    """Unparse a single prompt to STL lines."""
    lines = []

    # Header with context info
    if prompt.get("context"):
        ctx_parts = []
        for k, v in prompt["context"].items():
            if isinstance(v, str):
                ctx_parts.append(f'{k}="{v}"')
            else:
                ctx_parts.append(f"{k}={v}")
        lines.append(f"// context: {', '.join(ctx_parts)}")

    display_name = name
    lines.append(f"prompt {display_name} {{")

    indent = "  "

    # Annotations
    for d in prompt.get("desc", []):
        lines.append(f'{indent}annotate {fmt_string(d)};')

    # Fields
    emit_fields(prompt.get("fields", []), indent, lines)

    # Channels
    emit_channels(prompt.get("channels", []), indent, lines)

    # Flows
    emit_flows(prompt.get("flows", []), indent, lines)

    # Return
    emit_return(prompt.get("return"), indent, lines)

    lines.append("}")
    return "\n".join(lines)


def unparse_record(name, record):
    """Unparse a record definition (for reference)."""
    lines = []
    lines.append(f"record {name} {{")

    indent = "  "

    for d in record.get("desc", []):
        lines.append(f'{indent}annotate {fmt_string(d)};')

    fields = record.get("fields", [])
    if len(fields) == 1 and fields[0].get("format"):
        # Simple record: `is format;`
        f = fields[0]
        fmt = fmt_format(f["format"])
        if fmt:
            lines.append(f"{indent}is {fmt};")
    else:
        emit_fields(fields, indent, lines)

    lines.append("}")
    return "\n".join(lines)


def unparse_ir(ir):
    """Unparse an IR JSON dict to STL source."""
    sections = []

    # Entry points as comments
    entry_points = ir.get("entry_points", {})
    if entry_points:
        for alias, target in entry_points.items():
            if alias != target:
                sections.append(f"export {target} as {alias};")
            else:
                sections.append(f"// entry: {alias}")

    # Records (for reference, as comments)
    records = ir.get("records", {})
    if records:
        sections.append("// ---- Records (instantiated, for reference) ----")
        for name, rec in sorted(records.items()):
            sections.append("")
            sections.append(unparse_record(name, rec))

    # Prompts
    prompts = ir.get("prompts", {})
    if prompts:
        sections.append("")
        sections.append("// ---- Prompts (fully instantiated) ----")
        for name, pmt in sorted(prompts.items()):
            sections.append("")
            sections.append(unparse_prompt(name, pmt))

    return "\n".join(sections) + "\n"


# ============================================================================
# Main
# ============================================================================

def main():
    if len(sys.argv) > 1 and sys.argv[1] != "-":
        with open(sys.argv[1]) as f:
            ir = json.load(f)
    else:
        ir = json.load(sys.stdin)

    print(unparse_ir(ir))


if __name__ == "__main__":
    main()
