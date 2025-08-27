#ifndef AUTOCOG_PARSER_STL_IR_SEARCH_HXX
#define AUTOCOG_PARSER_STL_IR_SEARCH_HXX

namespace autocog { namespace parser {

DATA(Param) {
  NODES(Identifier) locator;
  NODE(Expression)  value;
};

DATA(Search) {
  NODES(Param) params;
};

} }

#endif // AUTOCOG_PARSER_STL_IR_SEARCH_HXX
