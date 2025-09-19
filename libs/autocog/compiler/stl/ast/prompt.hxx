#ifndef AUTOCOG_COMPILER_STL_AST_PROMPT_HXX
#define AUTOCOG_COMPILER_STL_AST_PROMPT_HXX

namespace autocog::compiler::stl::ast {

DATA(Prompt) {
  std::string     name;
  MAPPED(Define)  defines;
  ONODE(Annotate) annotate;
  ONODE(Search)   search;

  NODE(Struct)   fields;
  ONODE(Channel) channel;
  ONODE(Flow)    flow;
  ONODE(Return)  retstmt;

// TODO
//   NODE(Struct) fields;
//   VARIANTS(Define, Annotate, Search, Channel, Flow, Return) constructs;
};
TRAVERSE_CHILDREN(Prompt, defines, annotate, search, fields, channel, flow, retstmt)
// TRAVERSE_CHILDREN(Prompt, fields, constructs)

}

#endif // AUTOCOG_COMPILER_STL_AST_PROMPT_HXX
