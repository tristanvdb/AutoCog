#ifndef AUTOCOG_PARSER_STL_IR_FLOW_HXX
#define AUTOCOG_PARSER_STL_IR_FLOW_HXX

namespace autocog { namespace parser {

DATA(Edge) {
  NODE(Identifier) prompt;
  ONODE(String) label;
};

DATA(Flow) {
  bool single_statement; //< Keyword followed by single element instead of block
  NODES(Edge) edges;
};

} }

#endif // AUTOCOG_PARSER_STL_IR_FLOW_HXX
