#ifndef AUTOCOG_COMPILER_STL_AST_DEFINE_HXX
#define AUTOCOG_COMPILER_STL_AST_DEFINE_HXX

namespace autocog::compiler {

DATA(Define) {
  bool argument;
  std::string name;
  ONODE(Expression) init;
};

}

#endif // AUTOCOG_COMPILER_STL_AST_DEFINE_HXX
