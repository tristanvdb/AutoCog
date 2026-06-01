from autocog.errors import ConfigError
"""
Clause transformations — applied in parse order to channel data.
"""

import copy


def _apply_selector(data, step):
    """Apply a path step's selector to `data` after the name lookup.

    Selector forms (Python slice semantics):
      no index/slice key -> whole field (data unchanged)
      "index": i         -> data[i]      (scalar element)
      "slice": [lo, hi]  -> data[lo:hi]  (list; null bound = open)
    """
    if "index" in step and step["index"] is not None:
        if not isinstance(data, list):
            return None
        idx = step["index"]
        return data[idx] if -len(data) <= idx < len(data) else None
    if "slice" in step and step["slice"] is not None:
        if not isinstance(data, list):
            return None
        lo, hi = step["slice"]
        return data[lo:hi]   # open bounds (None) behave as Python slice defaults
    return data


def navigate(data, path):
    """Navigate a nested dict/list by path steps (name + optional selector)."""
    for step in path:
        name = step["name"]
        if isinstance(data, dict):
            data = data.get(name)
        else:
            return None
        if data is None:
            return None
        data = _apply_selector(data, step)
        if data is None:
            return None
    return data


def set_at_path(data, path, value):
    """Set a value at a path in a nested dict/list, creating intermediates."""
    if not path:
        return value
    current = data
    for step in path[:-1]:
        name = step["name"]
        idx = step.get("index")
        if isinstance(current, dict):
            if name not in current:
                current[name] = {}
            current = current[name]
        if idx is not None and isinstance(current, list):
            current = current[idx]
    last = path[-1]
    name = last["name"]
    idx = last.get("index")
    sl = last.get("slice")
    if isinstance(current, dict):
        current[name] = value
    elif isinstance(current, list) and idx is not None:
        current[idx] = value
    elif isinstance(current, list) and sl is not None:
        current[sl[0]:sl[1]] = value
    return data


def apply_bind(data, clause):
    """Extract source path and optionally rename to target path."""
    source = clause["source"]
    target = clause.get("target", [])
    value = navigate(data, source)
    if not target:
        return value
    result = copy.deepcopy(data) if isinstance(data, dict) else data
    if isinstance(result, dict):
        # Remove source
        src_name = source[0]["name"] if source else None
        if src_name and src_name in result:
            del result[src_name]
        # Set target
        tgt_name = target[0]["name"] if target else None
        if tgt_name:
            result[tgt_name] = value
    return result


def apply_ravel(data, clause):
    """Flatten nested arrays."""
    target = clause.get("target", [])
    depth = clause.get("depth", 1)

    def flatten(lst, d):
        if d <= 0 or not isinstance(lst, list):
            return lst
        result = []
        for item in lst:
            if isinstance(item, list):
                result.extend(flatten(item, d - 1))
            else:
                result.append(item)
        return result

    if target:
        value = navigate(data, target)
        if isinstance(value, list):
            return set_at_path(copy.deepcopy(data), target, flatten(value, depth))
        return data
    else:
        if isinstance(data, list):
            return flatten(data, depth)
        return data


def apply_wrap(data, clause):
    """Wrap a value into a single-element list."""
    target = clause.get("target", [])
    if target:
        value = navigate(data, target)
        return set_at_path(copy.deepcopy(data), target, [value])
    else:
        return [data]


def apply_prune(data, clause):
    """Remove a field from a record."""
    target = clause.get("target", [])
    if not target:
        return data
    result = copy.deepcopy(data) if isinstance(data, dict) else data
    if isinstance(result, dict) and len(target) == 1:
        name = target[0]["name"]
        result.pop(name, None)
    return result


def apply_mapped(data, clause):
    """
    Not a data transformation — returns the list to iterate over.
    The caller handles expansion into multiple calls.
    """
    target = clause.get("target", [])
    if target:
        return navigate(data, target)
    return data


def apply_clause(data, clause):
    """Apply a single clause to data."""
    t = clause["type"]
    if t == "bind":
        return apply_bind(data, clause)
    elif t == "ravel":
        return apply_ravel(data, clause)
    elif t == "wrap":
        return apply_wrap(data, clause)
    elif t == "prune":
        return apply_prune(data, clause)
    elif t == "mapped":
        # mapped is handled specially by the caller
        return data
    else:
        raise ConfigError(f"Unknown clause type: {t}")


def apply_clauses(data, clauses):
    """Apply a clause pipeline in order, skipping mapped (handled by caller)."""
    for clause in clauses:
        if clause["type"] != "mapped":
            data = apply_clause(data, clause)
    return data


def get_mapped_clauses(clauses):
    """Extract mapped clauses from a pipeline."""
    return [c for c in clauses if c["type"] == "mapped"]
