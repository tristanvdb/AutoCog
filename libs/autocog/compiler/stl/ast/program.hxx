#ifndef AUTOCOG_COMPILER_STL_AST_PROGRAM_HXX
#define AUTOCOG_COMPILER_STL_AST_PROGRAM_HXX

namespace autocog::compiler::stl::ast {

DATA(Export) {
  std::string alias;
  std::string target;
  MAPPED(Expression) kwargs;
};
TRAVERSE_CHILDREN(Export, kwargs)

DATA(Import) {
  std::string file;
  std::unordered_map<std::string,std::string> targets;
};
TRAVERSE_CHILDREN_EMPTY(Import)

DATA(Program) {
  std::string filename;

  NODES(Import) imports{};
  NODES(Export) exports{};

  MAPPED(Define)  defines{};
  ONODE(Annotate) annotate{};
  ONODE(Search)   search{};

  MAPPED(Record) records{};
  MAPPED(Prompt) prompts{};

// TODO VARIANTS(Import, Export, Define, Annotate, Search, Record, Prompt) statements;
};
TRAVERSE_CHILDREN(Program, imports, exports, defines, annotate, search, records, prompts)
// TODO TRAVERSE_CHILDREN(Program, statements)

}

#endif // AUTOCOG_COMPILER_STL_AST_PROGRAM_HXX
