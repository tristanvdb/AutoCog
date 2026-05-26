#ifndef AUTOCOG_COMPILER_STL_AST_SEARCH_HXX
#define AUTOCOG_COMPILER_STL_AST_SEARCH_HXX

namespace autocog::compiler::stl::ast {

DATA(Param) {
  NODES(Identifier) locator;
  NODE(Expression)  value;
};

DATA(Search) {
  NODES(Param) params;
};

}

#endif // AUTOCOG_COMPILER_STL_AST_SEARCH_HXX
