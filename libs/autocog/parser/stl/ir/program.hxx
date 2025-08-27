#ifndef AUTOCOG_PARSER_STL_IR_PROGRAM_HXX
#define AUTOCOG_PARSER_STL_IR_PROGRAM_HXX

namespace autocog { namespace parser {

DATA(Entry) {
  NODE(Identifier) alias;
  NODE(String) target;
};

DATA(Import) {
  NODE(String) file;
  NODES(Identifier) prompts;
  NODES(Identifier) records;
};

DATA(Program) {
//DATA_CTOR_EMPTY(Program) :
//  imports(),
//  defines(),
//  annotate(std::nullopt),
//  search(std::nullopt),
//  entries(),
//  records(),
//  prompts()
//{}

  NODES(Import) imports;

  MAPPED(Define)  defines;
  ONODE(Annotate) annotate;
  ONODE(Search)   search;

  MAPPED(Entry)  entries;
  MAPPED(Record) records;
  MAPPED(Prompt) prompts;
};

EXEC(Program) {
  EXEC_CTOR(Program) {}

  void queue_imports(std::queue<std::string> & queue) const;
};

} }

#endif // AUTOCOG_PARSER_STL_IR_PROGRAM_HXX
