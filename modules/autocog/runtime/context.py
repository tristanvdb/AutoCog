"""The execution context a Python entrypoint runs inside.

    from autocog.runtime import context, extern

    @extern
    def pick(choices, answer, **kwargs):
        base = 0 if context.syntax.prompt_zero_index else 1
        return choices[int(answer) - base]

Every callable an STL program names must carry `@extern`. It is
MANDATORY and not merely conventional: it is where a resolver will
eventually declare what it needs — `@extern(capabilities=["interrupt"])`
— and a capability that is not declared is ambient authority, which is
the form nobody can audit. Making it mandatory before there is a corpus
of undecorated resolvers costs a dozen edits; making it mandatory later
costs a migration.

The decorator does not wrap. The function stays exactly as callable as
it was, so a resolver remains a plain function you can call from a test
with no machinery. All it does is mark, and carry the declaration.

## What `context` is, and what it is NOT

A ContextVar, not a global. Set around each extern call and reset after,
so it is per-async-task: a campaign runs an EnginePool PER SYNTAX in one
process with several samples in flight, and module-level state would let
a resolver under one syntax read another's. That is a torn read
producing a plausible wrong answer — a strictly worse version of the bug
this was built to fix.

Contents are STATIC for a run: the syntax, the program, the prompt being
resolved. Not the search status, not the tree, not the node. That line
is deliberate: a resolver reading only static configuration stays a pure
function of (frame, config), so an answer can be re-derived from a
stored frame without replaying the run. The dynamic namespaces would end
that, and they need hook points that do not exist — node-time and
frontier-time callbacks, whose cost lands in the C++ hot path.

## Ambient is for observing; a binding is for meaning

Anything a resolver's RETURN VALUE depends on should be a call binding,
declared in the STL and recorded in the STA:

    return call pick _ { zero_index use __syntax__.prompt_zero_index; };

The index base is exactly that, which is why it is bound rather than
read from here — the campaign's results depend on it, so the dependency
belongs in the artifact where a graph and a fingerprint can see it.

`context` is for what a binding cannot express: introspection,
diagnostics, and later the capabilities. To make the difference
checkable rather than remembered, attribute reads are RECORDED on the
call (`record_call(..., context_reads=[...])`). A resolver that turns out
to be reading `syntax.prompt_zero_index` is telling you it should have
been a binding.
"""

import contextvars

from autocog.errors import ConfigError

#: per-task, not global: several samples under DIFFERENT syntaxes are in
#: flight in one process during a campaign
_CURRENT = contextvars.ContextVar("autocog_context", default=None)

#: marks a callable as an STL entrypoint; see load_externals
MARKER = "__autocog_extern__"


def extern(fn=None, *, capabilities=()):
    """Mark a Python callable as an STL entrypoint.

    `capabilities` is the declaration channel for what a resolver may do
    beyond returning a value. Nothing grants anything yet — it exists now
    so that the day one does, every entrypoint already has somewhere to
    say so, and an undeclared capability is a load error rather than a
    silent reach into the runtime.
    """
    def mark(f):
        setattr(f, MARKER, {"capabilities": tuple(capabilities)})
        return f
    return mark if fn is None else mark(fn)


def is_extern(fn):
    return getattr(fn, MARKER, None) is not None


class _View:
    """A read-logging view over one namespace of the context."""

    __slots__ = ("_name", "_data", "_observe")

    def __init__(self, name, data, observe):
        object.__setattr__(self, "_name", name)
        object.__setattr__(self, "_data", data or {})
        object.__setattr__(self, "_observe", observe)

    def __getattr__(self, key):
        data = object.__getattribute__(self, "_data")
        name = object.__getattribute__(self, "_name")
        if key not in data:
            # Not None: a missing field flowing into a resolver as if it
            # were a value is how the rendering knobs went unnoticed.
            raise ConfigError(
                f"context.{name} has no field {key!r} (have {sorted(data)})")
        object.__getattribute__(self, "_observe")(f"{name}.{key}")
        return data[key]

    def __repr__(self):
        return f"<context.{object.__getattribute__(self, '_name')}>"


class _Context:
    """What is in scope while an entrypoint runs."""

    __slots__ = ("prompt", "_syntax", "_program", "_reads")

    def __init__(self, prompt=None, syntax=None, program=None):
        self.prompt = prompt
        self._reads = []
        self._syntax = _View("syntax", syntax, self._reads.append)
        self._program = _View("program", program, self._reads.append)

    @property
    def syntax(self):
        return self._syntax

    @property
    def program(self):
        return self._program

    @property
    def reads(self):
        """What this call read, in order. The evidence for promoting an
        ambient read to a declared binding."""
        return list(self._reads)


class _Proxy:
    """`context` at module scope, resolving to the current task's."""

    def _current(self):
        ctx = _CURRENT.get()
        if ctx is None:
            raise ConfigError(
                "autocog.runtime.context is only available while an "
                "entrypoint is running. A resolver called directly must be "
                "passed what it needs as arguments.")
        return ctx

    def __getattr__(self, key):
        return getattr(self._current(), key)

    def __repr__(self):
        return "<autocog.runtime.context>"


context = _Proxy()


def bind(prompt=None, syntax=None, program=None):
    """Context manager installing a context for the duration of a call.

    Yields it, so the caller can read `.reads` afterwards and record what
    the resolver actually touched."""
    class _Bound:
        def __enter__(self_):
            self_.token = _CURRENT.set(_Context(prompt, syntax, program))
            return _CURRENT.get()

        def __exit__(self_, *exc):
            _CURRENT.reset(self_.token)
            return False

    return _Bound()
