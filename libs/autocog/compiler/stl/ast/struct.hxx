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

DATA(FormatRef) {
  VARIANT(Identifier, Text, Enum, Choice) type;
  NODES(Expression) args;
  NODES(Assign) kwargs;
};
TRAVERSE_CHILDREN(FormatRef, type, args, kwargs)

DATA(Struct) {
  PNODES(Field) fields;
  // An inline/anonymous struct may also carry the constructs that describe its
  // fields: `search` (exploration policy) and `annotate` (documentation). It
  // does NOT accept `define` (a named-scope-only declaration) or prompt-only
  // constructs (channel/flow/return).
  VARIANTS(Annotate, Search) constructs;
};
TRAVERSE_CHILDREN(Struct, fields, constructs)

DATA(Field) {
  NODE(Identifier) name;
  ONODE(Expression) lower;
  ONODE(Expression) upper;
  VARIANT(FormatRef, Struct) type;
};
TRAVERSE_CHILDREN(Field, name, lower, upper, type)

}

#endif // AUTOCOG_COMPILER_STL_AST_STRUCT_HXX
