#ifndef AUTOCOG_COMPILER_STL_AST_DEFINE_HXX
#define AUTOCOG_COMPILER_STL_AST_DEFINE_HXX

namespace autocog::compiler::stl::ast {

DATA(Define) {
  bool argument;
  std::string name;
  ONODE(Expression) init;
};
TRAVERSE_CHILDREN(Define, init)

}

#endif // AUTOCOG_COMPILER_STL_AST_DEFINE_HXX
