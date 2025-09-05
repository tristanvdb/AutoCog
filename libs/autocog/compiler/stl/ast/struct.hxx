#ifndef AUTOCOG_COMPILER_STL_AST_STRUCT_HXX
#define AUTOCOG_COMPILER_STL_AST_STRUCT_HXX

namespace autocog::compiler {

DATA(Enum) {
  NODES(String) enumerators;
};

enum class ChoiceKind { Repeat, Select };

DATA(Choice) {
  ChoiceKind mode;
  NODE(Path) source;
};

DATA(Text) {
  // TODO vocab definition
};

DATA(Format) {
  VARIANT(Identifier, Text, Enum, Choice) type;
  NODES(Expression) args;
  MAPPED(Expression) kwargs;
};

DATA(Struct) {
  PNODES(Field) fields;
};

DATA(Field) {
  std::string name;
  ONODE(Expression) lower;
  ONODE(Expression) upper;
  VARIANT(Format, Struct) type;
};

}

#endif // AUTOCOG_COMPILER_STL_AST_STRUCT_HXX
