#ifndef AUTOCOG_COMPILER_STL_AST_RECORD_HXX
#define AUTOCOG_COMPILER_STL_AST_RECORD_HXX

namespace autocog::compiler {

DATA(Record) {
  std::string name;
  MAPPED(Define)  defines;
  ONODE(Annotate) annotate;
  ONODE(Search)   search;

  VARIANT(Struct, Format) record;
};

}

#endif // AUTOCOG_COMPILER_STL_AST_RECORD_HXX
