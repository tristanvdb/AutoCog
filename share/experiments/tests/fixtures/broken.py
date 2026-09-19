"""A resolver that always raises — the arm the preflight must catch.

Decorated like any entrypoint: what is broken here is the CALL, not the
contract. An undecorated function fails at load instead, which is a
different (and also caught) failure."""

from autocog.runtime import extern


@extern
def explode(**kwargs):
    raise RuntimeError("this arm cannot execute")
