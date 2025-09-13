
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

static ast::OpKind token_to_operator_kind(TokenType type) {
  switch (type) {
    case TokenType::BANG:     return ast::OpKind::Not;
    case TokenType::PLUS:     return ast::OpKind::Add;
    case TokenType::MINUS:    return ast::OpKind::Sub;
    case TokenType::STAR:     return ast::OpKind::Mul;
    case TokenType::SLASH:    return ast::OpKind::Div;
    case TokenType::PERCENT:  return ast::OpKind::Mod;
    case TokenType::AMPAMP:   return ast::OpKind::And;
    case TokenType::PIPEPIPE: return ast::OpKind::Or;
    case TokenType::LT:       return ast::OpKind::Lt;
    case TokenType::GT:       return ast::OpKind::Gt;
    case TokenType::LTEQ:     return ast::OpKind::Lte;
    case TokenType::GTEQ:     return ast::OpKind::Gte;
    case TokenType::EQEQ:     return ast::OpKind::Eq;
    case TokenType::BANGEQ:   return ast::OpKind::Neq;
    default:                  return ast::OpKind::NOP;
  }
}

[[maybe_unused]] static int get_precedence(TokenType type) {
  switch (type) {
    case TokenType::STAR:
    case TokenType::SLASH:
    case TokenType::PERCENT:
      return 10;  // Multiplicative
      
    case TokenType::PLUS:
    case TokenType::MINUS:
      return 9;   // Additive
      
    case TokenType::LT:
    case TokenType::GT:
    case TokenType::LTEQ:
    case TokenType::GTEQ:
      return 8;   // Relational
      
    case TokenType::EQEQ:
    case TokenType::BANGEQ:
      return 7;   // Equality
      
    case TokenType::AMPAMP:
      return 4;   // Logical AND
      
    case TokenType::PIPEPIPE:
      return 3;   // Logical OR
      
    case TokenType::QUESTION:
      return 2;   // Ternary conditional
      
    default:
      return -1;  // Not a binary operator
  }
}

static bool is_unary(TokenType tok) {
  return tok == TokenType::BANG || tok == TokenType::MINUS;
}

static bool is_binary(TokenType tok) {
  auto kind = token_to_operator_kind(tok);
  return kind != ast::OpKind::Not && kind != ast::OpKind::NOP;
}

static bool is_primary(TokenType tok) {
  return tok == TokenType::STRING_LITERAL  ||
         tok == TokenType::INTEGER_LITERAL ||
         tok == TokenType::FLOAT_LITERAL   ||
         tok == TokenType::BOOLEAN_LITERAL ||
         tok == TokenType::IDENTIFIER;
}

[[maybe_unused]] static bool is_conditional(TokenType tok) {
  return tok == TokenType::QUESTION;
}

#define DEBUG_parse_primary VERBOSE && 0

static void parse_primary(ParserState & state, ast::Data<ast::Tag::Expression> & expr) {
#if DEBUG_parse_primary
  std::cerr << "parse_primary" << std::endl;
#endif
  switch (state.current.type) {
    case TokenType::IDENTIFIER: {
      state.advance();
      expr.expr.emplace<0>();
      auto & data = std::get<0>(expr.expr).data;
      data.name = state.previous.text;
      break;
    }
    
    case TokenType::INTEGER_LITERAL: {
      state.advance();
      expr.expr.emplace<1>();
      auto & data = std::get<1>(expr.expr).data;
      data.value = std::stoi(state.previous.text);
      break;
    }
    
    case TokenType::FLOAT_LITERAL: {
      state.advance();
      expr.expr.emplace<2>();
      auto & data = std::get<2>(expr.expr).data;
      data.value = std::stof(state.previous.text);
      break;
    }
    
    case TokenType::BOOLEAN_LITERAL: {
      state.advance();
      expr.expr.emplace<3>();
      auto & data = std::get<3>(expr.expr).data;
      data.value = (state.previous.text == "true");
      break;
    }
    
    case TokenType::STRING_LITERAL: {
      state.advance();
      expr.expr.emplace<4>();
      auto & data = std::get<4>(expr.expr).data;
      clean_raw_string(state.previous.text, data);
      break;
    }
    
    default:
      state.throw_error("Expected literal or identifier in expression.");
      break;
  }
}

#define DEBUG_Parse_Expression VERBOSE && 0

template <>
void Parser::parse<ast::Tag::Expression>(ParserState & state, ast::Data<ast::Tag::Expression> & expr) {
#if DEBUG_Parse_Expression
  std::cerr << "Parser::parse<ast::Tag::Expression>" << std::endl;
#endif
  if (is_primary(state.current.type)) {
    parse_primary(state, expr);

  } else if (is_unary(state.current.type)) {
    state.advance();
    expr.expr.emplace<5>();
    auto & data = std::get<5>(expr.expr).data;
    data.kind = token_to_operator_kind(state.previous.type);
    if (data.kind == ast::OpKind::Sub) data.kind = ast::OpKind::Neg;
    data.operand = std::make_unique<ast::Expression>();
    if (!is_primary(state.current.type) && state.current.type != TokenType::LPAREN ) {
      state.throw_error("Unary operator expects primary or parenthesized operand!");
    }
    parse(state, data.operand->data);
  } else if (state.match(TokenType::LPAREN)) {
    auto operand = std::make_unique<ast::Expression>();
    parse(state, operand->data);
    if (is_binary(state.current.type)) {
      state.advance();
      expr.expr.emplace<6>();
      auto & data = std::get<6>(expr.expr).data;
      data.kind = token_to_operator_kind(state.previous.type);
      data.lhs = std::move(operand);
      data.rhs = std::make_unique<ast::Expression>();
      parse(state, data.rhs->data);

    } else if (state.match(TokenType::QUESTION)) {
      expr.expr.emplace<7>();
      auto & data = std::get<7>(expr.expr).data;
      data.cond = std::move(operand);
      data.e_true = std::make_unique<ast::Expression>();
      parse(state, data.e_true->data);
      state.expect(TokenType::COLON, " within conditional expression.");
      data.e_false = std::make_unique<ast::Expression>();
      parse(state, data.e_false->data);

    } else {
      expr.expr.emplace<8>();
      auto & data = std::get<8>(expr.expr).data;
      data.expr = std::move(operand);

    }
    state.expect(TokenType::RPAREN, " to end parenthesized expression.");
    
  } else {
    state.throw_error("Expression must be primary, unary, or parenthesized (for binary and conditional)!");
  }
}

}

