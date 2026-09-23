#ifndef AUTOCOG_COMPILER_STL_AST_SEARCH_HXX
#define AUTOCOG_COMPILER_STL_AST_SEARCH_HXX

namespace autocog::compiler::stl::ast {

DATA(Param) {
  NODES(Identifier) locator;
  /// One value for scalar params; several (comma-separated) for
  /// registry-marked list params such as queue.metric.
  NODES(Expression) values;
};
TRAVERSE_CHILDREN(Param, locator, values)

DATA(Search) {
  NODES(Param) params;
  /// Postfix `on` clause: `search { ... } on a.b, c, _;` — each target is a
  /// field path or `_` (a one-step path named "_", the enclosing scope).
  /// Empty = plain block, sugar for `on _;`.
  NODES(Path) targets;
};
TRAVERSE_CHILDREN(Search, params, targets)

}

#endif // AUTOCOG_COMPILER_STL_AST_SEARCH_HXX
