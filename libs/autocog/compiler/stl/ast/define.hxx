#ifndef AUTOCOG_COMPILER_STL_AST_DEFINE_HXX
#define AUTOCOG_COMPILER_STL_AST_DEFINE_HXX

namespace autocog::compiler::stl::ast {

DATA(Define) {
  bool is_argument;
  NODE(Identifier) name;
  ONODE(Expression) init;
};
TRAVERSE_CHILDREN(Define, name, init)

}

#endif // AUTOCOG_COMPILER_STL_AST_DEFINE_HXX
