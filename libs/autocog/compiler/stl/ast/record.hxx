#ifndef AUTOCOG_COMPILER_STL_AST_RECORD_HXX
#define AUTOCOG_COMPILER_STL_AST_RECORD_HXX

namespace autocog::compiler::stl::ast {

DATA(Record) {
  NODE(Identifier) name;
  VARIANT(Struct, Format) record;
  VARIANTS(Define, Annotate, Search, Alias) constructs;
};
TRAVERSE_CHILDREN(Record, name, record, constructs)

}

#endif // AUTOCOG_COMPILER_STL_AST_RECORD_HXX
