#ifndef AUTOCOG_PARSER_STL_IR_PROGRAM_HXX
#define AUTOCOG_PARSER_STL_IR_PROGRAM_HXX

namespace autocog { namespace parser {

DATA(Export) {
  // Syntax: "export my_alias is my_target<arg=expr, arg=expr>;"
  std::string alias;
  std::string target;
  MAPPED(Expression) kwargs;
};

DATA(Import) {
  // Syntax: "from file import target as alias, target;"
  std::string file;
  std::unordered_map<std::string,std::string> targets;
};

DATA(Program) {
  NODES(Import)  imports;
  MAPPED(Export) exports;

  MAPPED(Define)  defines;
  ONODE(Annotate) annotate;
  ONODE(Search)   search;

  MAPPED(Record) records;
  MAPPED(Prompt) prompts;
};

EXEC(Program) {
  EXEC_CTOR(Program) {}

  void queue_imports(std::queue<std::string> & queue) const;
};

} }

#endif // AUTOCOG_PARSER_STL_IR_PROGRAM_HXX
