#ifndef AUTOCOG_PARSER_STL_IR_EXPR_HXX
#define AUTOCOG_PARSER_STL_IR_EXPR_HXX

namespace autocog {
namespace parser {

enum class UnaryKind { Not, Neg };
enum class BinaryKind { Add, Sub, Mul, Div, Mod, And, Or, Xor };

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

DATA(Scalar) {
  VARIANT(Identifier, Integer, Float, Boolean, String) value;
};

DATA(Unary) {
  UnaryKind kind;
  PNODE(Expression) operand;
};

DATA(Binary) {
  BinaryKind kind;
  PNODE(Expression) lhs;
  PNODE(Expression) rhs;
};

DATA(Conditional) {
  PNODE(Expression) cond;
  PNODE(Expression) e_true;
  PNODE(Expression) e_false;
};

DATA(Expression) {
  VARIANT(Scalar, Unary, Binary, Conditional, Identifier) expr;
};

} }

#endif // AUTOCOG_PARSER_STL_IR_EXPR_HXX
