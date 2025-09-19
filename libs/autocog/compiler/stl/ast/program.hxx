#ifndef AUTOCOG_COMPILER_STL_AST_PROGRAM_HXX
#define AUTOCOG_COMPILER_STL_AST_PROGRAM_HXX

namespace autocog::compiler::stl::ast {

DATA(Alias) {
  NODE(ObjectRef) target;
  ONODE(Identifier) alias;
  bool is_export;
};
TRAVERSE_CHILDREN(Alias, target, alias)

DATA(Import) {
  std::string file;
  NODES(Alias) targets;
};
TRAVERSE_CHILDREN(Import, targets)

DATA(Program) {
  std::string filename;
  VARIANTS(Import, Alias, Define, Annotate, Search, Record, Prompt) statements{};
};
TRAVERSE_CHILDREN(Program, statements)

}

#endif // AUTOCOG_COMPILER_STL_AST_PROGRAM_HXX
