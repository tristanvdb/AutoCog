#ifndef AUTOCOG_PARSER_STL_IR_RECORD_HXX
#define AUTOCOG_PARSER_STL_IR_RECORD_HXX

namespace autocog { namespace parser {

DATA(Record) {
  std::string name;
  MAPPED(Define)  defines;
  ONODE(Annotate) annotate;
  ONODE(Search)   search;

  VARIANT(Struct, Format) record;
};

} }

#endif // AUTOCOG_PARSER_STL_IR_RECORD_HXX
