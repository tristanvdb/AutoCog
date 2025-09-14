#ifndef AUTOCOG_COMPILER_STL_AST_ANNOT_HXX
#define AUTOCOG_COMPILER_STL_AST_ANNOT_HXX

namespace autocog::compiler::stl::ast {

DATA(Annotation) {
  ONODE(Path) path;
  NODE(Expression) description;
};

DATA(Annotate) {
  bool single_statement; //< Keyword followed by single element instead of block
  NODES(Annotation) annotations;
};

}

#endif // AUTOCOG_COMPILER_STL_AST_ANNOT_HXX
