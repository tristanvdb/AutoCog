#ifndef AUTOCOG_COMPILER_STL_AST_PROMPT_HXX
#define AUTOCOG_COMPILER_STL_AST_PROMPT_HXX

namespace autocog::compiler {

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

}

#endif // AUTOCOG_COMPILER_STL_AST_PROMPT_HXX
