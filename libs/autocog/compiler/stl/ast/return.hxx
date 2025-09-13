#ifndef AUTOCOG_COMPILER_STL_AST_RETURN_HXX
#define AUTOCOG_COMPILER_STL_AST_RETURN_HXX

namespace autocog::compiler::stl::ast {

DATA(Retfield) {
  ONODE(Expression) alias; // if missing implies `field.steps[-1].field` (meaning "C" for path "A[0:2].B.C[3]")
  VARIANT(Path, Expression) source;
  VARIANTS(Bind, Ravel, Wrap, Prune) clauses;
};

DATA(Return) {
  ONODE(Expression) label;
  bool short_form;
  NODES(Retfield) fields;
};

/**
 * Block form does not accept "_" as a field alias
 * 
 * Short form:
 *   `return use field.field;`
 *   `return "label" use field.field;`
 * is equivalent to:
 *   `return { "_" use field.field; }`
 *   `return "label" { "_" use field.field; }`
 * 
 * Empty return is also possible:
 *   `return;`
 *   `return "label";`
 * and is equivalent to:
 *   `return {}`
 *   `return "label" {}`
 */


}

#endif // AUTOCOG_COMPILER_STL_AST_RETURN_HXX
