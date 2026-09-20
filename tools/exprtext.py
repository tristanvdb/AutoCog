"""The text form of an expression, shared by the tools that print one.

A description, a search parameter or an argument is an expression
(libs/autocog/data/expr.hxx): a literal in nearly every program, and a tree
when it asks about something that is not known until the prompt is
instantiated. `expr_text` renders either back to STL source -- the mirror of
data::Expr::str() -- so a tool that shows a program shows what was written
rather than a JSON node.
"""


def _num(v):
    if isinstance(v, float):
        s = f"{v:.3f}".rstrip("0").rstrip(".")
        return s if s else "0"
    return str(v)


def _value(v):
    if isinstance(v, str):
        return '"' + v.replace('"', '\\"') + '"'
    if isinstance(v, bool):
        return "true" if v else "false"
    if v is None:
        return "null"
    return _num(v)


# The STL spelling of each expression operator, keyed by its wire name (the
# mirror of data::op_symbol; see libs/autocog/data/expr.cxx).
_OPS = {
    "not": "!", "neg": "-",
    "add": "+", "sub": "-", "mul": "*", "div": "/", "mod": "%",
    "and": "&&", "or": "||", "all": "&&", "any": "||",
    "band": "&", "bor": "|",
    "lt": "<", "gt": ">", "le": "<=", "ge": ">=", "eq": "==", "ne": "!=",
}


def expr_text(e):
    """Render an expression back to STL source.

    A literal is its value, which is what nearly every expression in an IR is.
    Anything else is a one-key object -- a reference that could not be resolved
    at compile time, or an operator over its operands. Fully parenthesized, so
    it reparses to the same tree."""
    if not isinstance(e, dict):
        return _value(e)
    (key, arg), = e.items()
    if key == "ref":
        return arg
    if key == "cond":
        c, t, f = (expr_text(o) for o in arg)
        return f"({c} ? {t} : {f})"
    if key == "cat":
        # An f-string unparses as one when every interpolation is a plain name,
        # because that is what the f-string grammar accepts; anything more has
        # no interpolation syntax to go back to and unparses as concatenation.
        if all(isinstance(o, str) or (isinstance(o, dict) and "ref" in o)
               for o in arg):
            out = ""
            for o in arg:
                if isinstance(o, str):
                    out += o.replace('"', '\\"').replace("{", "{{").replace("}", "}}")
                else:
                    out += "{" + o["ref"] + "}"
            return 'f"' + out + '"'
        text = expr_text(arg[-1])
        for o in reversed(arg[:-1]):
            text = "(" + expr_text(o) + " + " + text + ")"
        return text
    sym = _OPS.get(key)
    if sym is None:
        raise ValueError(f"not an expression operator: {key}")
    if key in ("not", "neg"):
        return f"({sym}{expr_text(arg[0])})"
    return "(" + f" {sym} ".join(expr_text(o) for o in arg) + ")"
