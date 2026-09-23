#ifndef AUTOCOG_COMPILER_STL_AST_EXPR_HXX
#define AUTOCOG_COMPILER_STL_AST_EXPR_HXX

namespace autocog::compiler::stl::ast {

// The operator set is the shared one (autocog::data): the AST is a way of
// SPELLING an expression, not a second expression language, and the semantics
// that go with these operators live with the representation that outlives the
// compiler.
using OpKind = autocog::data::OpKind;
using autocog::data::opKindToString;

DATA(Identifier) {
  std::string name;
};
TRAVERSE_CHILDREN_EMPTY(Identifier)

DATA(Integer) {
  int value;
};
TRAVERSE_CHILDREN_EMPTY(Integer)

DATA(Float) {
  float value;
};
TRAVERSE_CHILDREN_EMPTY(Float)

DATA(Boolean) {
  bool value;
};
TRAVERSE_CHILDREN_EMPTY(Boolean)

DATA(String) {
  std::string value;
  bool is_format{false};
};
TRAVERSE_CHILDREN_EMPTY(String)

DATA(Unary) {
  OpKind kind;
  PNODE(Expression) operand;
};
TRAVERSE_CHILDREN(Unary, operand)

DATA(Binary) {
  OpKind kind;
  PNODE(Expression) lhs;
  PNODE(Expression) rhs;
};
TRAVERSE_CHILDREN(Binary, lhs, rhs)

DATA(Conditional) {
  PNODE(Expression) cond;
  PNODE(Expression) e_true;
  PNODE(Expression) e_false;
};
TRAVERSE_CHILDREN(Conditional, cond, e_true, e_false)

DATA(Parenthesis) {
  PNODE(Expression) expr;
};
TRAVERSE_CHILDREN(Parenthesis, expr)

// Vocab leaves. `tokenize(s, ...)` is the union of the tokens produced by each
// string; `regex(s)` is the set of tokens whose surface matches. Their string
// arguments are String nodes (literals / f-strings), like enum enumerators.
DATA(Tokenize) {
  NODES(String) strings;
};
TRAVERSE_CHILDREN(Tokenize, strings)

DATA(Regex) {
  NODE(String) pattern;
};
TRAVERSE_CHILDREN(Regex, pattern)

DATA(Expression) {
  VARIANT(Identifier, Integer, Float, Boolean, String, Unary, Binary, Conditional, Parenthesis, Tokenize, Regex) expr;
};
TRAVERSE_CHILDREN(Expression, expr)

}

#endif // AUTOCOG_COMPILER_STL_AST_EXPR_HXX
