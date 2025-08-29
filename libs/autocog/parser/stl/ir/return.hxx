#ifndef AUTOCOG_PARSER_STL_IR_RETURN_HXX
#define AUTOCOG_PARSER_STL_IR_RETURN_HXX

namespace autocog { namespace parser {

DATA(Retfield) {
  NODE(Path)    field;
  ONODE(String) alias;
};

DATA(Return) {
  ONODE(String)   label;
  NODES(Retfield) fields;
};

} // namespace parser  
} // namespace autocog

#endif // AUTOCOG_PARSER_STL_IR_RETURN_HXX
