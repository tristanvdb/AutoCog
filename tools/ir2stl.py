#!/usr/bin/env python3
"""Unparse an IR JSON document (from `stlc --ir`) back into STL*.

STL* is the normalized form of an STL program: search policies are propagated
and collapsed onto fields, self-form records are inlined, and every field is
rendered as a self-form record `name is { is <fmt>; search { ... } }`. The
output is itself valid STL, so it can be fed back through stlc; doing so should
be idempotent (STL* in -> STL* out).

Usage:
    stlc --ir /dev/stdout program.stl | python ir2stl.py
    python ir2stl.py program.ir.json [prompt_name]
"""
import json
import sys


def _indent(level):
    return "  " * level


def _num(v):
    # Render numbers without trailing noise; ints stay ints, floats keep up to
    # 3 decimals (matching the generated-JSON number policy).
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float):
        s = f"{v:.3f}".rstrip("0").rstrip(".")
        return s if s else "0"
    return str(v)


def _value(v):
    if isinstance(v, str):
        return '"' + v.replace('"', '\\"') + '"'
    return _num(v)


def unparse_search(search, level):
    """Render a search policy dict {category: {param: value}} as a search { }
    block. Categories and params are emitted in sorted order for stable output.
    Returns a list of lines, or [] if the policy is empty."""
    if not search:
        return []
    lines = [_indent(level) + "search {"]
    for category in sorted(search.keys()):
        params = search[category]
        for param in sorted(params.keys()):
            loc = f"{category}.{param}" if category else param
            lines.append(f"{_indent(level + 1)}{loc} is {_value(params[param])};")
    lines.append(_indent(level) + "}")
    return lines


def unparse_format(fmt):
    """Render a leaf format as its `is <fmt>` right-hand side (no trailing
    semicolon, no `is` keyword — just the format text)."""
    t = fmt.get("type")
    if t == "completion":
        args = []
        if fmt.get("length") is not None:
            args.append(f"length={fmt['length']}")
        if fmt.get("vocab"):
            args.append(f"vocab={fmt['vocab']}")
        return "text" + (f"<{', '.join(args)}>" if args else "")
    if t == "enum":
        vals = ", ".join('"' + v + '"' for v in fmt.get("values", []))
        return f"enum({vals})"
    if t == "choice":
        steps = fmt.get("path", {}).get("steps", [])
        path = ".".join(s["name"] for s in steps)
        return f"{fmt.get('mode', 'select')}({path})"
    return "text"  # fallback


def _range_suffix(rng):
    if not rng:
        return ""
    lo, hi = rng[0], rng[1]
    return f"[{lo}]" if lo == hi else f"[{lo}:{hi}]"


def unparse_field(field, level):
    """Render a field as a self-form record:
        name[range] is {
          // from Refname        (if collapsed from a named record)
          is <fmt>;              (leaf)   OR   <sub-fields...>   (struct)
          search { ... }         (if any policy)
        }
    """
    name = field["name"]
    rng = _range_suffix(field.get("range"))
    lines = [f"{_indent(level)}{name}{rng} is {{"]
    inner = level + 1

    if field.get("refname"):
        lines.append(f"{_indent(inner)}// from {field['refname']}")

    # Field annotations.
    for d in field.get("desc", []):
        lines.append(f'{_indent(inner)}annotate is "{d}";')

    fmt = field.get("format") or {}
    if fmt.get("type") == "struct":
        for sub in fmt.get("fields", []):
            lines.extend(unparse_field(sub, inner))
    else:
        lines.append(f"{_indent(inner)}is {unparse_format(fmt)};")

    lines.extend(unparse_search(field.get("search"), inner))
    lines.append(_indent(level) + "}")
    return lines


def _vocab_tree_to_str(node):
    """Render a vocab expression tree (the IR's structured form) back to its
    STL* string: full operators and parentheses (mirrors VocabExpr::str())."""
    kind = node.get("kind")
    if kind == "tokenize":
        inner = ", ".join('"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
                          for s in node.get("strings", []))
        return f"tokenize({inner})"
    if kind == "regex":
        ss = node.get("strings", [])
        p = ss[0] if ss else ""
        p = p.replace("\\", "\\\\").replace('"', '\\"')
        return f'regex("{p}")'
    ops = node.get("operands", [])
    if kind == "union":      return f"({_vocab_tree_to_str(ops[0])} | {_vocab_tree_to_str(ops[1])})"
    if kind == "intersect":  return f"({_vocab_tree_to_str(ops[0])} & {_vocab_tree_to_str(ops[1])})"
    if kind == "diff":       return f"({_vocab_tree_to_str(ops[0])} - {_vocab_tree_to_str(ops[1])})"
    if kind == "complement": return f"(!{_vocab_tree_to_str(ops[0])})"
    return ""


def unparse_prompt(name, prompt):
    lines = [f"prompt {name} {{"]
    # Vocab table: emit each entry as a prompt-scope `vocab vocab_<hash> = <expr>;`
    # declaration; fields reference these by name. Sorted for stable output.
    vocabs = prompt.get("vocabs") or {}
    for vkey in sorted(vocabs):
        lines.append(f"{_indent(1)}vocab {vkey} = {_vocab_tree_to_str(vocabs[vkey])};")
    lines.append(_indent(1) + "is {")
    for field in prompt.get("fields", []):
        lines.extend(unparse_field(field, 2))
    lines.append(_indent(1) + "}")
    # Prompt-scope policy (flow / queue).
    lines.extend(unparse_search(prompt.get("search"), 1))
    lines.append("}")
    return "\n".join(lines)


def main():
    if len(sys.argv) > 1 and sys.argv[1] not in ("-", "--"):
        with open(sys.argv[1]) as f:
            ir = json.load(f)
        want = sys.argv[2] if len(sys.argv) > 2 else None
    else:
        ir = json.load(sys.stdin)
        want = sys.argv[2] if len(sys.argv) > 2 else None

    prompts = ir.get("prompts", {})
    out = []
    for mangled, prompt in prompts.items():
        # STL* identifies prompts by their mangled name (the post-instantiation
        # identity). Records in the IR are debug/instantiation-result carriers
        # and are intentionally not unparsed.
        name = prompt.get("mangled_name", mangled)
        if want and name != want:
            continue
        out.append(unparse_prompt(name, prompt))

    # Entry points: emit `export <mangled> as <entry>;` only when the names
    # differ. A prompt whose name already equals the entry is its own implicit
    # entry, so an explicit export would redeclare it.
    exports = []
    for entry, mangled in (ir.get("entry_points") or {}).items():
        if mangled != entry:
            exports.append(f"export {mangled} as {entry};")

    body = "\n\n".join(out)
    if exports:
        body += ("\n\n" if body else "") + "\n".join(sorted(exports))
    print(body)


if __name__ == "__main__":
    main()
