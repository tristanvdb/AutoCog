#ifndef AUTOCOG_PARSER_STL_IR_DEFINE_HXX
#define AUTOCOG_PARSER_STL_IR_DEFINE_HXX

namespace autocog { namespace parser {

DATA(Define) {
  bool argument;
  std::string name;
  ONODE(Expression) init;
};

} }

#endif // AUTOCOG_PARSER_STL_IR_DEFINE_HXX
