"""
Channel resolution — builds the content dict for a prompt invocation.

Async-native: call channels run whole sub-flows (awaiting the engine per
prompt) and mapped calls fan their jobs out concurrently via gather — the
source of intra-sample parallelism when the engine dispatches to remote
lanes. Channels themselves resolve strictly in order (a later channel's
kwargs may self-reference the content built so far).
"""

import asyncio
import copy
import inspect
import itertools

from .clauses import navigate, apply_clauses, get_mapped_clauses, apply_mapped
from .errors import ConfigError

#: Frame keys that belong to the machinery rather than to the schema the author
#: wrote. `next` carries the flow choice (see Context.step_async); a spread must
#: not hand it to a callee as if it were data.
FLOW_FIELDS = frozenset({"next"})


def resolve_path(source, path):
    """Navigate a dict/list by STA path steps.

    Thin alias over clauses.navigate so the index/slice selector semantics live
    in one place (previously this was a second, single-index-only copy).
    """
    return navigate(source, path)


def insert_at_target(content, target_path, value):
    """Insert a value into the content dict at the target path."""
    if not target_path:
        return
    current = content
    for step in target_path[:-1]:
        name = step["name"]
        if name not in current:
            current[name] = {}
        current = current[name]
    last = target_path[-1]
    current[last["name"]] = value


def resolve_input(channel, inputs):
    """Resolve an input channel: read from external inputs.

    The whole path, not just its first step: `get choice.value` reads the
    values out of a list of records. This used to take `source[0]` and stop,
    so a dotted input path silently delivered the wrong thing — the enclosing
    records rather than the field named."""
    if not channel["source"]:
        return None
    return resolve_path(inputs, channel["source"])


def resolve_dataflow(channel, frames, current_prompt):
    """Resolve a dataflow channel: read from another prompt's latest frame."""
    prompt = channel.get("prompt")
    if prompt is None:
        # Self-reference: read from own previous frame
        prompt = current_prompt

    frame = frames.get(prompt)
    if frame is None:
        return None

    data = resolve_path(frame, channel["source"])
    if data is not None and channel.get("clauses"):
        data = apply_clauses(data, channel["clauses"])
    return data


#: syntax_id -> the syntax dict. The datastore is content-addressed, so an
#: id is stable for identical content and this cannot go stale.
_SYNTAX_CACHE = {}


def syntax_of(engine):
    """The syntax an engine renders with, as a dict.

    Engine, RemoteBackend and EnginePool share no base class but all carry a
    `syntax_id` -- instantiation happens client-side even for a remote
    backend -- so this is the one accessor that works for every runner."""
    # An engine that knows its syntax without the datastore answers first:
    # a level-2 client instantiates nothing locally, so it has no id and
    # fetches the server's instead.
    own = getattr(engine, "syntax", None)
    if isinstance(own, dict):
        return own
    sid = getattr(engine, "syntax_id", None)
    if sid is None:
        return None
    if sid not in _SYNTAX_CACHE:
        from .runtime.sta import runtime_sta_cxx

        _SYNTAX_CACHE[sid] = runtime_sta_cxx.get_syntax(sid)
    return _SYNTAX_CACHE[sid]


def resolve_argument(expr, engine):
    """A call argument's value expression -> its value for THIS run.

    Nearly always a literal, in which case this is the value. When it is not,
    it is an expression the program could not finish: a syntax is chosen when a
    prompt is instantiated, and the same program renders under 64 of them in
    one campaign, so a resolver that has to invert a rendering decision --
    `select` emits a position, and prompt_zero_index decides whether that
    position is spelled from 0 or from 1 -- can only get it here.

    The evaluation is the C++ one, through a binding: the expression semantics
    exist once (libs/autocog/data/expr.cxx) and this is a door onto them, not a
    second implementation that can drift.

    An unknown field is an ERROR, raised by the evaluator. The alternative is
    None flowing into a resolver as if it were a value, which is how the
    rendering knobs went unnoticed in the first place.
    """
    if not isinstance(expr, dict):
        return expr                       # a literal, which is the usual case

    syntax = syntax_of(engine)
    if not isinstance(syntax, dict):
        raise ConfigError(
            f"{expr!r} needs the engine's syntax, which this runner did not "
            f"provide")
    from .runtime.sta import runtime_sta_cxx

    try:
        return runtime_sta_cxx.eval_expr(expr, {"__syntax__": syntax})
    except ConfigError:
        raise
    except Exception as e:                # the evaluator's own diagnostics
        raise ConfigError(str(e)) from e


def resolve_call_kwargs(kwargs, inputs, frames, current_prompt, content=None,
                        engine=None):
    """Resolve call kwargs, handling mapped expansion.

    content: the partially-built content dict for the current prompt,
    used for self-referencing kwargs (use .field).
    engine: needed only by kwargs whose value expression is not a literal --
    a `__syntax__.*` reference is a property of the run, not of the program.
    """
    resolved = {}
    mapped_axes = {}  # kwarg_name → list of values to iterate

    for kw in kwargs:
        name = kw["name"]
        if kw.get("value") is not None:
            # The argument's value: `is <expr>` or `use <namespace>.<field>`.
            data = resolve_argument(kw["value"], engine)
        elif kw["is_input"]:
            data = resolve_path(inputs, kw["path"])
        else:
            prompt = kw.get("prompt")
            if prompt:
                # Read from another prompt's frame
                frame = frames.get(prompt, {})
            else:
                # Self-reference: read from content being built
                frame = content if content is not None else frames.get(current_prompt, {})
            data = resolve_path(frame, kw["path"])

        # Check for mapped clauses
        mapped = get_mapped_clauses(kw.get("clauses", []))
        if mapped:
            # Get the iteration axis
            iterable = apply_mapped(data, mapped[0])
            if isinstance(iterable, list):
                mapped_axes[name] = iterable
            else:
                mapped_axes[name] = [iterable]
            # Apply non-mapped clauses to each element later
        else:
            # Apply non-mapped clauses
            if kw.get("clauses"):
                data = apply_clauses(data, kw["clauses"])
            resolved[name] = data

    return resolved, mapped_axes


def expand_mapped(base_kwargs, mapped_axes):
    """
    Expand mapped kwargs into a list of kwarg dicts.
    Multiple mapped kwargs create a cartesian product.
    """
    if not mapped_axes:
        return [base_kwargs]

    keys = list(mapped_axes.keys())
    value_lists = [mapped_axes[k] for k in keys]

    jobs = []
    for combo in itertools.product(*value_lists):
        job = copy.deepcopy(base_kwargs)
        for key, val in zip(keys, combo):
            job[key] = val
        jobs.append(job)

    return jobs


async def resolve_call(channel, inputs, frames, current_prompt, engine, program,
                       externals, content=None, recorder=None, ctx_id=None):
    """
    Resolve a call channel.

    1. Resolve kwargs (with mapped expansion)
    2. Execute call(s) — prompt sub-flow or Python callable — mapped jobs
       concurrently (they are independent: inputs fixed at expansion)
    3. Apply link-level clauses to results
    """
    spread = channel.get("spread")
    base_kwargs, mapped_axes = {}, {}
    if spread is not None:
        # `call f <path>` — the record at <path> IS the argument list, `_` (an
        # empty path) being the root: for a return, the prompt's frame.
        root = content if content is not None else frames.get(current_prompt, {})
        record = resolve_path(root, spread) if spread else root
        if not isinstance(record, dict):
            raise ConfigError(
                f"call spread expects a record, got {type(record).__name__}")
        base_kwargs = {k: v for k, v in record.items() if k not in FLOW_FIELDS}
    if channel.get("kwargs"):
        # `call f _ { k use ... }` — the spread is the default and a named
        # argument overrides it. A resolver needing one thing the frame does
        # not carry should not have to give up the spread and list every
        # field by hand.
        named, mapped_axes = resolve_call_kwargs(
            channel["kwargs"], inputs, frames, current_prompt, content,
            engine=engine)
        base_kwargs.update(named)
    jobs = expand_mapped(base_kwargs, mapped_axes)

    entry = channel.get("entry")
    extern = channel.get("extern")
    target = channel.get("target", [])
    field_name = ".".join(t["name"] for t in target)

    if extern and extern not in externals:
        # For a Python call `entry` mirrors `extern`, so falling through would
        # look up a prompt by that name and die with a bare KeyError. A
        # resolver that failed to load is a broken program: say so here.
        raise ConfigError(
            f"call names the Python callable '{extern}', which is not loaded. "
            f"Available: {sorted(externals)}")

    async def one_job(job):
        if extern and extern in externals:
            # Python callable (sync or async), run inside a context. A
            # ContextVar rather than module state: a campaign has samples
            # under DIFFERENT syntaxes in flight in one process, and a
            # global would let one resolver read another's.
            from .runtime.context import bind

            with bind(prompt=current_prompt, syntax=syntax_of(engine),
                      program=getattr(program, "sta", None)) as ctx:
                result = externals[extern](**job)
                if inspect.isawaitable(result):
                    result = await result
            if recorder:
                # What the resolver READ from the ambient context. Recorded
                # because it is the evidence for promoting an ambient read
                # to a declared call binding: anything the return value
                # depends on belongs in the STA, not in here.
                recorder.record_call(ctx_id, field_name, callable_name=extern,
                                     context_reads=ctx.reads or None)
            return result
        elif entry:
            # Sub-prompt execution
            from .context import Context
            ctx = Context(program, engine, entry, job, externals,
                          recorder=recorder, parent_ctx=ctx_id)
            while not ctx.done:
                await ctx.step_async()
            if recorder:
                recorder.record_call(ctx_id, field_name,
                                     prompt_name=entry, sub_ctx=ctx.ctx_id)
            return ctx.result
        return None

    results = list(await asyncio.gather(*(one_job(job) for job in jobs)))

    # If mapped, collect results as a list; otherwise single result
    if mapped_axes:
        data = results
    else:
        data = results[0] if results else None

    # Apply link-level clauses
    if channel.get("clauses"):
        data = apply_clauses(data, channel["clauses"])

    return data


async def resolve_channels(program, prompt_name, inputs, frames, engine, externals,
                           recorder=None, ctx_id=None):
    """
    Resolve all channels for a prompt, building the content dict.
    """
    content = {}
    channels = program.prompt_channels(prompt_name)

    for ch in channels:
        ch_type = ch["type"]
        target = ch["target"]

        if ch_type == "input":
            value = resolve_input(ch, inputs)
        elif ch_type == "dataflow":
            value = resolve_dataflow(ch, frames, prompt_name)
        elif ch_type == "call":
            value = await resolve_call(ch, inputs, frames, prompt_name, engine,
                                       program, externals, content,
                                       recorder=recorder, ctx_id=ctx_id)
        else:
            continue

        if value is not None:
            insert_at_target(content, target, value)

    return content
