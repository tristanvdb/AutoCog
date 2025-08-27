#ifndef AUTOCOG_PARSER_STL_IR_RETURN_HXX
#define AUTOCOG_PARSER_STL_IR_RETURN_HXX

namespace autocog { namespace parser {

DATA(Return) {
  ONODE(String) label;
  MAPPED(Path) fields;
};

} // namespace parser  
} // namespace autocog

#endif // AUTOCOG_PARSER_STL_IR_RETURN_HXX
