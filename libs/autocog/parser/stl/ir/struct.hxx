#ifndef AUTOCOG_PARSER_STL_IR_STRUCT_HXX
#define AUTOCOG_PARSER_STL_IR_STRUCT_HXX

namespace autocog { namespace parser {

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

} }

#endif // AUTOCOG_PARSER_STL_IR_STRUCT_HXX
