#ifndef AUTOCOG_COMPILER_STL_AST_STRUCT_HXX
#define AUTOCOG_COMPILER_STL_AST_STRUCT_HXX

namespace autocog::compiler::stl::ast {

DATA(Enum) {
  NODES(String) enumerators;
};
TRAVERSE_CHILDREN(Enum, enumerators)

enum class ChoiceKind { Repeat, Select };

DATA(Choice) {
  ChoiceKind mode;
  NODE(Path) source;
};
TRAVERSE_CHILDREN(Choice, source)

DATA(Text) {
  // TODO vocab definition
};
TRAVERSE_CHILDREN_EMPTY(Text)

DATA(Format) {
  VARIANT(Identifier, Text, Enum, Choice) type;
  NODES(Expression) args;
  NODES(Assign) kwargs;
};
TRAVERSE_CHILDREN(Format, type, args, kwargs)

DATA(Struct) {
  PNODES(Field) fields;
};
TRAVERSE_CHILDREN(Struct, fields)

DATA(Field) {
  NODE(Identifier) name;
  ONODE(Expression) lower;
  ONODE(Expression) upper;
  VARIANT(Format, Struct) type;
};
TRAVERSE_CHILDREN(Field, name, lower, upper, type)

}

#endif // AUTOCOG_COMPILER_STL_AST_STRUCT_HXX
