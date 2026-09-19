"""A resolver that always raises — the arm the preflight must catch."""


def explode(**kwargs):
    raise RuntimeError("this arm cannot execute")
