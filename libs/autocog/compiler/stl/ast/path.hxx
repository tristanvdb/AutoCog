#ifndef AUTOCOG_COMPILER_STL_AST_PATH_HXX
#define AUTOCOG_COMPILER_STL_AST_PATH_HXX

namespace autocog::compiler::stl {

DATA(Step) {
  NODE(Identifier) field;
  ONODE(Expression) lower;
  ONODE(Expression) upper;
};

DATA(Path) {
  bool input;
  ONODE(Identifier) prompt;
  NODES(Step) steps;
};

}

#endif // AUTOCOG_COMPILER_STL_AST_PATH_HXX
