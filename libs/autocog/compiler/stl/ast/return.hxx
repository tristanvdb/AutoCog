#ifndef AUTOCOG_COMPILER_STL_AST_RETURN_HXX
#define AUTOCOG_COMPILER_STL_AST_RETURN_HXX

namespace autocog::compiler::stl::ast {

DATA(Retfield) {
  ONODE(Expression) alias; // if missing implies `field.steps[-1].field` (meaning "C" for path "A[0:2].B.C[3]")
  // "use" (a field), "is" (a compile-time value), or "call" (a callable).
  // A return call runs after the prompt's frame is stored, so `use .x` inside
  // it sees the generated value — the same spelling a channel uses to read the
  // pre-filled one. It is the program's own hook for turning what the model
  // emitted into what the caller wants, which is why nothing upstream of the
  // frame ever rewrites a field.
  VARIANT(Path, Expression, Call) source;
  VARIANTS(Bind, Ravel, Wrap, Prune) clauses;
};
TRAVERSE_CHILDREN(Retfield, alias, source, clauses)

DATA(Return) {
  ONODE(Expression) label;
  bool short_form;
  NODES(Retfield) fields;
};
TRAVERSE_CHILDREN(Return, label, fields)

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
