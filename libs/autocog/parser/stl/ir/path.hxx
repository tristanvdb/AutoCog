#ifndef AUTOCOG_PARSER_STL_IR_PATH_HXX
#define AUTOCOG_PARSER_STL_IR_PATH_HXX

namespace autocog { namespace parser {

DATA(Step) {
  NODE(Identifier) field;
  ONODE(Expression) lower;
  ONODE(Expression) upper;
};

DATA(Path) {
  bool input;
  ONODE(Identifier) prompt;
  NODES(Step) steps;
};

} }

#endif // AUTOCOG_PARSER_STL_IR_PATH_HXX
