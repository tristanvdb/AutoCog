#ifndef AUTOCOG_COMPILER_STL_AST_RETURN_HXX
#define AUTOCOG_COMPILER_STL_AST_RETURN_HXX

namespace autocog::compiler::stl {

DATA(Retfield) {
  NODE(Path)    field;
  ONODE(String) alias;
};

DATA(Return) {
  ONODE(String)   label;
  NODES(Retfield) fields;
};

}

#endif // AUTOCOG_COMPILER_STL_AST_RETURN_HXX
