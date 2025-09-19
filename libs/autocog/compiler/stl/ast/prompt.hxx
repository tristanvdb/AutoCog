#ifndef AUTOCOG_COMPILER_STL_AST_PROMPT_HXX
#define AUTOCOG_COMPILER_STL_AST_PROMPT_HXX

namespace autocog::compiler::stl::ast {

DATA(Prompt) {
  NODE(Identifier) name;
  ONODE(Struct) fields;
  VARIANTS(Define, Annotate, Search, Alias, Channel, Flow, Return) constructs;
};
TRAVERSE_CHILDREN(Prompt, name, fields, constructs)

}

#endif // AUTOCOG_COMPILER_STL_AST_PROMPT_HXX
