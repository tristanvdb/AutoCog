#ifndef AUTOCOG_COMPILER_STL_AST_RECORD_HXX
#define AUTOCOG_COMPILER_STL_AST_RECORD_HXX

namespace autocog::compiler::stl::ast {

DATA(Record) {
  std::string name;
  MAPPED(Define)  defines;
  ONODE(Annotate) annotate;
  ONODE(Search)   search;

  VARIANT(Struct, Format) record;

// TODO
//   VARIANT(Struct, Format) record;
//   VARIANTS(Define, Annotate, Search) constructs;
};
TRAVERSE_CHILDREN(Record, defines, annotate, search, record)
// TRAVERSE_CHILDREN(Record, record, constructs)

}

#endif // AUTOCOG_COMPILER_STL_AST_RECORD_HXX
