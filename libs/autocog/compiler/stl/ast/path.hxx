#ifndef AUTOCOG_COMPILER_STL_AST_PATH_HXX
#define AUTOCOG_COMPILER_STL_AST_PATH_HXX

namespace autocog::compiler::stl::ast {

DATA(Step) {
  NODE(Identifier) field;
  ONODE(Expression) lower;
  ONODE(Expression) upper;
  bool is_range;
};
TRAVERSE_CHILDREN(Step, field, lower, upper)

DATA(Path) {
  NODES(Step) steps;
};
TRAVERSE_CHILDREN(Path, steps)

DATA(Assign) {
  NODE(Identifier) argument;
  NODE(Expression) value;
};
TRAVERSE_CHILDREN(Assign, argument, value)

DATA(ObjectRef) {
  NODE(Identifier) name;
  NODES(Assign) config;
};
TRAVERSE_CHILDREN(ObjectRef, name, config)

DATA(FieldRef) {
  ONODE(ObjectRef) prompt;
  NODE(Path) field;
};
TRAVERSE_CHILDREN(FieldRef, prompt, field)

}

#endif // AUTOCOG_COMPILER_STL_AST_PATH_HXX
