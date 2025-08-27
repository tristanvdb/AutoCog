#ifndef AUTOCOG_PARSER_STL_IR_PROMPT_HXX
#define AUTOCOG_PARSER_STL_IR_PROMPT_HXX

namespace autocog { namespace parser {

DATA(Prompt) {
  std::string     name;
  MAPPED(Define)  defines;
  ONODE(Annotate) annotate;
  ONODE(Search)   search;

  NODE(Struct)   fields;
  ONODE(Channel) channel;
  ONODE(Flow)    flow;
  ONODE(Return)  retstmt;
};

} }

#endif // AUTOCOG_PARSER_STL_IR_PROMPT_HXX
