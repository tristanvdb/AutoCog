"""
Unit tests for autocog.clauses — clause transformation pipeline.
"""

import pytest

import importlib.util
import pathlib

# Import clauses.py directly — avoids autocog.__init__ which requires C++ bindings
_clauses_path = pathlib.Path(__file__).parent.parent.parent.parent.parent / "modules" / "autocog" / "clauses.py"
_spec = importlib.util.spec_from_file_location("autocog.clauses", _clauses_path)
_mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mod)

apply_bind = _mod.apply_bind
apply_ravel = _mod.apply_ravel
apply_wrap = _mod.apply_wrap
apply_prune = _mod.apply_prune
apply_mapped = _mod.apply_mapped
apply_clause = _mod.apply_clause
apply_clauses = _mod.apply_clauses
get_mapped_clauses = _mod.get_mapped_clauses
navigate = _mod.navigate
set_at_path = _mod.set_at_path


class TestNavigate:

    def test_simple_field(self):
        data = {"name": "Alice", "age": 25}
        assert navigate(data, [{"name": "name"}]) == "Alice"

    def test_nested_field(self):
        data = {"person": {"name": "Alice"}}
        assert navigate(data, [{"name": "person"}, {"name": "name"}]) == "Alice"

    def test_array_index(self):
        data = {"items": ["a", "b", "c"]}
        assert navigate(data, [{"name": "items", "index": 1}]) == "b"

    def test_missing_field(self):
        data = {"name": "Alice"}
        assert navigate(data, [{"name": "missing"}]) is None

    def test_empty_path(self):
        data = {"name": "Alice"}
        assert navigate(data, []) == data


class TestBind:

    def test_extract_field(self):
        data = {"name": "Alice", "age": 25}
        clause = {"type": "bind", "source": [{"name": "name"}], "target": []}
        result = apply_bind(data, clause)
        assert result == "Alice"

    def test_rename_field(self):
        data = {"old_name": "value"}
        clause = {"type": "bind", "source": [{"name": "old_name"}], "target": [{"name": "new_name"}]}
        result = apply_bind(data, clause)
        assert "new_name" in result
        assert result["new_name"] == "value"
        assert "old_name" not in result

    def test_extract_underscore(self):
        data = {"_": "direct_value", "other": "x"}
        clause = {"type": "bind", "source": [{"name": "_"}], "target": []}
        result = apply_bind(data, clause)
        assert result == "direct_value"

    def test_rename_underscore_to_named(self):
        data = {"_": "value"}
        clause = {"type": "bind", "source": [{"name": "_"}], "target": [{"name": "result"}]}
        result = apply_bind(data, clause)
        assert result["result"] == "value"


class TestRavel:

    def test_flat_list(self):
        # Already flat — no change
        data = ["a", "b", "c"]
        clause = {"type": "ravel", "depth": 1, "target": []}
        result = apply_ravel(data, clause)
        assert result == ["a", "b", "c"]

    def test_nested_list(self):
        data = [["a", "b"], ["c", "d"]]
        clause = {"type": "ravel", "depth": 1, "target": []}
        result = apply_ravel(data, clause)
        assert result == ["a", "b", "c", "d"]

    def test_deep_nested(self):
        data = [[["a", "b"], ["c"]], [["d"]]]
        clause = {"type": "ravel", "depth": 1, "target": []}
        result = apply_ravel(data, clause)
        assert result == [["a", "b"], ["c"], ["d"]]

    def test_depth_2(self):
        data = [[["a", "b"], ["c"]], [["d"]]]
        clause = {"type": "ravel", "depth": 2, "target": []}
        result = apply_ravel(data, clause)
        assert result == ["a", "b", "c", "d"]

    def test_with_target(self):
        data = {"items": [["a", "b"], ["c", "d"]], "meta": "x"}
        clause = {"type": "ravel", "depth": 1, "target": [{"name": "items"}]}
        result = apply_ravel(data, clause)
        assert result["items"] == ["a", "b", "c", "d"]
        assert result["meta"] == "x"

    def test_scalar_unchanged(self):
        data = "hello"
        clause = {"type": "ravel", "depth": 1, "target": []}
        result = apply_ravel(data, clause)
        assert result == "hello"


class TestWrap:

    def test_wrap_scalar(self):
        data = "hello"
        clause = {"type": "wrap", "target": []}
        result = apply_wrap(data, clause)
        assert result == ["hello"]

    def test_wrap_dict(self):
        data = {"name": "Alice"}
        clause = {"type": "wrap", "target": []}
        result = apply_wrap(data, clause)
        assert result == [{"name": "Alice"}]

    def test_wrap_with_target(self):
        data = {"title": "hello", "meta": "x"}
        clause = {"type": "wrap", "target": [{"name": "title"}]}
        result = apply_wrap(data, clause)
        assert result["title"] == ["hello"]
        assert result["meta"] == "x"


class TestPrune:

    def test_remove_field(self):
        data = {"name": "Alice", "secret": "hidden", "age": 25}
        clause = {"type": "prune", "target": [{"name": "secret"}]}
        result = apply_prune(data, clause)
        assert "name" in result
        assert "age" in result
        assert "secret" not in result

    def test_remove_nonexistent(self):
        data = {"name": "Alice"}
        clause = {"type": "prune", "target": [{"name": "missing"}]}
        result = apply_prune(data, clause)
        assert result == {"name": "Alice"}

    def test_no_target(self):
        data = {"name": "Alice"}
        clause = {"type": "prune", "target": []}
        result = apply_prune(data, clause)
        assert result == {"name": "Alice"}


class TestMapped:

    def test_map_list(self):
        data = ["a", "b", "c"]
        clause = {"type": "mapped", "target": []}
        result = apply_mapped(data, clause)
        assert result == ["a", "b", "c"]

    def test_map_with_target(self):
        data = {"items": ["a", "b", "c"], "meta": "x"}
        clause = {"type": "mapped", "target": [{"name": "items"}]}
        result = apply_mapped(data, clause)
        assert result == ["a", "b", "c"]


class TestApplyClauses:

    def test_empty_pipeline(self):
        data = {"name": "Alice"}
        result = apply_clauses(data, [])
        assert result == {"name": "Alice"}

    def test_bind_then_wrap(self):
        data = {"inner": {"value": "hello"}}
        clauses = [
            {"type": "bind", "source": [{"name": "inner"}, {"name": "value"}], "target": []},
            {"type": "wrap", "target": []},
        ]
        result = apply_clauses(data, clauses)
        assert result == ["hello"]

    def test_mapped_skipped(self):
        """apply_clauses should skip mapped (handled by caller)."""
        data = ["a", "b"]
        clauses = [
            {"type": "mapped", "target": []},
            {"type": "wrap", "target": []},
        ]
        result = apply_clauses(data, clauses)
        # mapped skipped, wrap applied to the list
        assert result == [["a", "b"]]

    def test_get_mapped_clauses(self):
        clauses = [
            {"type": "bind", "source": [], "target": []},
            {"type": "mapped", "target": []},
            {"type": "ravel", "depth": 1, "target": []},
        ]
        mapped = get_mapped_clauses(clauses)
        assert len(mapped) == 1
        assert mapped[0]["type"] == "mapped"


class TestComposition:
    """Test realistic clause compositions from the STL fixtures."""

    def test_mapped_bind_ravel(self):
        """Writer pattern: mapped calls return [{_: "x"}, {_: "y"}], bind renames, ravel flattens."""
        # Simulate: 3 mapped calls each returning {_: "response_N"}
        results = [{"_": "resp_0"}, {"_": "resp_1"}, {"_": "resp_2"}]

        # bind(_, response) applied to each
        bind_clause = {"type": "bind", "source": [{"name": "_"}], "target": [{"name": "response"}]}
        bound = [apply_bind(r, bind_clause) for r in results]
        assert all("response" in r for r in bound)

        # ravel flattens (in this case results are dicts, not lists, so ravel is no-op)
        ravel_clause = {"type": "ravel", "depth": 1, "target": []}
        raveled = apply_ravel(bound, ravel_clause)
        assert len(raveled) == 3

    def test_prune_then_bind(self):
        data = {"name": "Alice", "secret": "hidden", "value": 42}
        clauses = [
            {"type": "prune", "target": [{"name": "secret"}]},
            {"type": "bind", "source": [{"name": "value"}], "target": []},
        ]
        result = apply_clauses(data, clauses)
        assert result == 42


class TestSetAtPath:
    """Test set_at_path for nested path insertion."""

    def test_set_simple(self):
        data = {"a": 1}
        result = set_at_path(data, [{"name": "b"}], 2)
        assert result["b"] == 2

    def test_set_nested(self):
        data = {"a": {}}
        result = set_at_path(data, [{"name": "a"}, {"name": "x"}], 42)
        assert result["a"]["x"] == 42

    def test_set_creates_intermediate(self):
        data = {}
        result = set_at_path(data, [{"name": "a"}, {"name": "b"}], "val")
        assert result["a"]["b"] == "val"

    def test_set_empty_path(self):
        data = {"a": 1}
        result = set_at_path(data, [], "replaced")
        assert result == "replaced"


class TestNavigateEdgeCases:
    """Test navigate edge cases for better coverage."""

    def test_list_without_index(self):
        """List accessed without index returns None."""
        data = {"items": ["a", "b"]}
        assert navigate(data, [{"name": "items"}, {"name": "x"}]) is None

    def test_nested_array_traversal(self):
        data = {"items": [{"val": "a"}, {"val": "b"}]}
        assert navigate(data, [{"name": "items", "index": 0}]) == {"val": "a"}

    def test_deep_nested(self):
        data = {"a": {"b": {"c": "deep"}}}
        assert navigate(data, [
            {"name": "a"}, {"name": "b"}, {"name": "c"}
        ]) == "deep"


class TestApplyClauseDispatch:
    """Test apply_clause for all types."""

    def test_dispatch_bind(self):
        data = {"x": 1}
        result = apply_clause(data, {"type": "bind", "source": [{"name": "x"}], "target": []})
        assert result == 1

    def test_dispatch_ravel(self):
        data = [[1], [2]]
        result = apply_clause(data, {"type": "ravel", "depth": 1, "target": []})
        assert result == [1, 2]

    def test_dispatch_wrap(self):
        result = apply_clause("x", {"type": "wrap", "target": []})
        assert result == ["x"]

    def test_dispatch_prune(self):
        data = {"a": 1, "b": 2}
        result = apply_clause(data, {"type": "prune", "target": [{"name": "b"}]})
        assert "b" not in result

    def test_dispatch_mapped(self):
        """Mapped is a no-op in apply_clause (handled by caller)."""
        data = [1, 2, 3]
        result = apply_clause(data, {"type": "mapped", "target": []})
        assert result == [1, 2, 3]
