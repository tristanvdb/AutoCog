#ifndef AUTOCOG_COMPILER_STL_AST_ANNOT_HXX
#define AUTOCOG_COMPILER_STL_AST_ANNOT_HXX

namespace autocog::compiler::stl::ast {

DATA(Annotation) {
  ONODE(Path) path;
  NODE(Expression) description;
};
TRAVERSE_CHILDREN(Annotation, path, description)

DATA(Annotate) {
  bool single_statement; //< Keyword followed by single element instead of block
  NODES(Annotation) annotations;
};
TRAVERSE_CHILDREN(Annotate, annotations)



}

#endif // AUTOCOG_COMPILER_STL_AST_ANNOT_HXX
