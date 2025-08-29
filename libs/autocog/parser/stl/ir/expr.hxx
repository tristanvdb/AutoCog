#ifndef AUTOCOG_PARSER_STL_IR_EXPR_HXX
#define AUTOCOG_PARSER_STL_IR_EXPR_HXX

namespace autocog {
namespace parser {

enum class OpKind {
    NOP,                         // Not an operator
    Not, Neg,                    // Unary
    Add, Sub, Mul, Div, Mod,     // Arithmetic
    And, Or,                     // Logical
    Lt, Gt, Lte, Gte, Eq, Neq    // Comparison
};

DATA(Identifier) {
  std::string name;
};

DATA(Integer) {
  int value;
};

DATA(Float) {
  float value;
};

DATA(Boolean) {
  bool value;
};

DATA(String) {
  std::string value;
  bool is_format{false};
};

DATA(Unary) {
  OpKind kind;
  PNODE(Expression) operand;
};

DATA(Binary) {
  OpKind kind;
  PNODE(Expression) lhs;
  PNODE(Expression) rhs;
};

DATA(Conditional) {
  PNODE(Expression) cond;
  PNODE(Expression) e_true;
  PNODE(Expression) e_false;
};

DATA(Parenthesis) {
  PNODE(Expression) expr;
};

DATA(Expression) {
  VARIANT(Identifier, Integer, Float, Boolean, String, Unary, Binary, Conditional, Parenthesis) expr;
};

} }

#endif // AUTOCOG_PARSER_STL_IR_EXPR_HXX
