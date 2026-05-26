#ifndef AUTOCOG_COMPILER_STL_AST_SEARCH_HXX
#define AUTOCOG_COMPILER_STL_AST_SEARCH_HXX

namespace autocog::compiler::stl::ast {

DATA(Param) {
  NODES(Identifier) locator;
  NODE(Expression)  value;
};
TRAVERSE_CHILDREN(Param, locator, value)

DATA(Search) {
  NODES(Param) params;
};
TRAVERSE_CHILDREN(Search, params)

}

#endif // AUTOCOG_COMPILER_STL_AST_SEARCH_HXX
