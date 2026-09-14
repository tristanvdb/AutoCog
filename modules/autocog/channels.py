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
    """Resolve an input channel: read from external inputs."""
    source_name = channel["source"][0]["name"] if channel["source"] else None
    if source_name and source_name in inputs:
        return inputs[source_name]
    return None


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


def resolve_call_kwargs(kwargs, inputs, frames, current_prompt, content=None):
    """Resolve call kwargs, handling mapped expansion.
    
    content: the partially-built content dict for the current prompt,
    used for self-referencing kwargs (use .field).
    """
    resolved = {}
    mapped_axes = {}  # kwarg_name → list of values to iterate

    for kw in kwargs:
        name = kw["name"]
        if kw.get("value") is not None:
            # Literal value (from "is" keyword)
            data = kw["value"]
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
    base_kwargs, mapped_axes = resolve_call_kwargs(
        channel["kwargs"], inputs, frames, current_prompt, content
    )
    jobs = expand_mapped(base_kwargs, mapped_axes)

    entry = channel.get("entry")
    extern = channel.get("extern")
    target = channel.get("target", [])
    field_name = ".".join(t["name"] for t in target)

    async def one_job(job):
        if extern and extern in externals:
            # Python callable (sync or async)
            result = externals[extern](**job)
            if inspect.isawaitable(result):
                result = await result
            if recorder:
                recorder.record_call(ctx_id, field_name, callable_name=extern)
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
