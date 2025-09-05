#ifndef AUTOCOG_COMPILER_STL_AST_FLOW_HXX
#define AUTOCOG_COMPILER_STL_AST_FLOW_HXX

namespace autocog::compiler::stl {

DATA(Edge) {
  NODE(Identifier) prompt;
  ONODE(String) label;
};

DATA(Flow) {
  bool single_statement; //< Keyword followed by single element instead of block
  NODES(Edge) edges;
};

}

#endif // AUTOCOG_COMPILER_STL_AST_FLOW_HXX
