
#include "autocog/compiler/stl/ast.hxx"

namespace autocog::compiler::stl::ast {

std::string opKindToString(ast::OpKind op) {
  switch (op) {
    case ast::OpKind::NOP: return "NOP";
    case ast::OpKind::Not: return "Not";
    case ast::OpKind::Neg: return "Neg";
    case ast::OpKind::Add: return "Add";
    case ast::OpKind::Sub: return "Sub";
    case ast::OpKind::Mul: return "Mul";
    case ast::OpKind::Div: return "Div";
    case ast::OpKind::Mod: return "Mod";
    case ast::OpKind::And: return "And";
    case ast::OpKind::Or:  return "Or";
    case ast::OpKind::Lt:  return "Lt";
    case ast::OpKind::Gt:  return "Gt";
    case ast::OpKind::Lte: return "Lte";
    case ast::OpKind::Gte: return "Gte";
    case ast::OpKind::Eq:  return "Eq";
    case ast::OpKind::Neq: return "Neq";
    default: return "Unknown(" + std::to_string(static_cast<int>(op)) + ")";
  }
}

}

