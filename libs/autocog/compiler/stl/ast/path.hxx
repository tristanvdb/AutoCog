#ifndef AUTOCOG_COMPILER_STL_AST_PATH_HXX
#define AUTOCOG_COMPILER_STL_AST_PATH_HXX

namespace autocog::compiler::stl::ast {

DATA(Step) {
  NODE(Identifier) field;
  ONODE(Expression) lower;
  ONODE(Expression) upper;
  bool bounds;
};

DATA(Path) {
  NODES(Step) steps;
};

DATA(PromptRef) {
  NODE(Identifier) name;
  MAPPED(Expression) config;
};

DATA(FieldRef) {
  ONODE(PromptRef) prompt;
  NODE(Path) field;
};

}

#endif // AUTOCOG_COMPILER_STL_AST_PATH_HXX
