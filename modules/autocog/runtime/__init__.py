"""Runtime services a Python entrypoint sees.

`extern` marks a callable an STL program may name — mandatory — and
`context` is what is in scope while one runs. See context.py for why the
decorator is required and where the line between an ambient read and a
declared call binding falls.
"""

from .context import bind, context, extern, is_extern

__all__ = ["bind", "context", "extern", "is_extern"]
