#ifndef AUTOCOG_COMPILER_STL_AST_EXPR_HXX
#define AUTOCOG_COMPILER_STL_AST_EXPR_HXX

namespace autocog::compiler::stl::ast {

enum class OpKind {
    NOP,                         // Not an operator
    Not, Neg,                    // Unary
    Add, Sub, Mul, Div, Mod,     // Arithmetic
    And, Or,                     // Logical
    Lt, Gt, Lte, Gte, Eq, Neq    // Comparison
};
std::string opKindToString(ast::OpKind op);

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

DATA(Expression) {
  VARIANT(Identifier, Integer, Float, Boolean, String, Unary, Binary, Conditional, Parenthesis) expr;
};
TRAVERSE_CHILDREN(Expression, expr)

}

#endif // AUTOCOG_COMPILER_STL_AST_EXPR_HXX
