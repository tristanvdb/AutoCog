
#include "autocog/compiler/stl/parser.hxx"
#include "autocog/compiler/stl/diagnostic.hxx"

#include <filesystem>

#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

static std::string get_line(std::string const & source, int line_pos) {
    if (line_pos <= 0) throw std::runtime_error("In get_line(): line number must be greater than 0");
    std::stringstream ss(source);
    int cnt = 0;
    std::string line;
    while (std::getline(ss, line)) {
        cnt++;
        if (cnt == line_pos) return line;
    }
    throw std::runtime_error("In get_line(): line number must be less than the number of lines in the file");
}

ParserState::ParserState(
  int fileid,
  std::string const & source_,
  std::list<Diagnostic> & diagnostics_
) :
  source(source_),
  diagnostics(diagnostics_),
  stream(source),
  lexer(stream),
  previous(),
  current(lexer.advance())
{
  lexer.set_file_id(fileid);
}

void ParserState::advance() {
  previous = current;
  current = lexer.advance();
}

bool ParserState::check(TokenType type) {
  return current.type == type;
}

bool ParserState::match(TokenType type) {
  if (check(type)) {
    advance();
    return true;
  } else {
    return false;
  }
}

bool ParserState::expect(TokenType type, std::string context) {
  if (match(type)) {
    return true;
  } else {
    std::ostringstream oss;
    oss << "Expected token `" << token_type_name(type) << "` but found `" << token_type_name(current.type) << "` " << context;
    emit_error(oss.str());
    return false;
  }
}

void ParserState::emit_error(std::string msg) {
  auto & loc = current.location;
  auto line = get_line(source, loc.line);
  diagnostics.emplace_back(DiagnosticLevel::Error, msg, line, loc);
}

Parser::Parser(
  std::list<Diagnostic> & diagnostics_,
  std::unordered_map<std::string, int> & fileids_,
  std::list<std::string> const & search_paths_
) :
  search_paths(search_paths_),
  diagnostics(diagnostics_),
  fileids(fileids_),
  queue(),
  programs()
{}

Parser::Parser(
  std::list<Diagnostic> & diagnostics_,
  std::unordered_map<std::string, int> & fileids_,
  std::list<std::string> const & search_paths_,
  std::list<std::string> const & filepaths
) :
  Parser(diagnostics_, fileids_, search_paths_)
{
  for (auto & filepath: filepaths) {
    queue.push(filepath);
  }
}

static std::string file_lookup(std::string const & filepath, std::list<std::string> const & search_paths) {
  std::string found_path;
  if (std::filesystem::exists(filepath)) {
    found_path = filepath;
  } else {
    for (auto const & search_path : search_paths) {
      std::filesystem::path full_path = std::filesystem::path(search_path) / filepath;
      if (std::filesystem::exists(full_path)) {
        found_path = full_path.string();
        break;
      }
    }
  }
  return found_path;
}

void Parser::parse() {
  while (!queue.empty()) {
    std::string filepath = queue.front();
    queue.pop();

    if (fileids.find(filepath) != fileids.end()) continue;
    int fid = fileids.size();
    fileids.emplace(filepath, fid);

    std::string found_path = file_lookup(filepath, search_paths);
    if (found_path.empty()) {
      std::ostringstream oss;
      oss << "Cannot find file: `" << filepath << "`";
      diagnostics.emplace_back(DiagnosticLevel::Error, oss.str());
    } else {
      std::ifstream file(found_path);
      std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      file.close();

      parse(fid, filepath, source);
      programs[filepath].exec.queue_imports(queue);
    }
  }
}

template <> void Parser::parse<ast::Tag::Expression> (ParserState & state, ast::Data<ast::Tag::Expression> &);
template <> void Parser::parse<ast::Tag::Program>    (ParserState & state, ast::Data<ast::Tag::Program>    &);
template <> void Parser::parse<ast::Tag::Prompt>     (ParserState & state, ast::Data<ast::Tag::Prompt>     &);
template <> void Parser::parse<ast::Tag::Record>     (ParserState & state, ast::Data<ast::Tag::Record>     &);
template <> void Parser::parse<ast::Tag::Format>     (ParserState & state, ast::Data<ast::Tag::Format>     &);
template <> void Parser::parse<ast::Tag::Channel>    (ParserState & state, ast::Data<ast::Tag::Channel>    &);
template <> void Parser::parse<ast::Tag::Flow>       (ParserState & state, ast::Data<ast::Tag::Flow>       &);
template <> void Parser::parse<ast::Tag::Return>     (ParserState & state, ast::Data<ast::Tag::Return>     &);
template <> void Parser::parse<ast::Tag::Struct>     (ParserState & state, ast::Data<ast::Tag::Struct>     &);
template <> void Parser::parse<ast::Tag::Field>      (ParserState & state, ast::Data<ast::Tag::Field>      &);
template <> void Parser::parse<ast::Tag::Search>     (ParserState & state, ast::Data<ast::Tag::Search>     &);
template <> void Parser::parse<ast::Tag::Annotate>   (ParserState & state, ast::Data<ast::Tag::Annotate>   &);
template <> void Parser::parse<ast::Tag::Define>     (ParserState & state, ast::Data<ast::Tag::Define>     &);
template <> void Parser::parse<ast::Tag::Export>     (ParserState & state, ast::Data<ast::Tag::Export>     &);
template <> void Parser::parse<ast::Tag::Path>       (ParserState & state, ast::Data<ast::Tag::Path>       &);
template <> void Parser::parse<ast::Tag::Import>     (ParserState & state, ast::Data<ast::Tag::Import>     &);

static void clean_raw_string(std::string raw_text, ast::Data<ast::Tag::String> & data) {
  if (raw_text.empty()) {
    throw std::runtime_error("Found empty string literal!");
  }
  data.is_format = (raw_text[0] == 'f' || raw_text[0] == 'F');
  if (data.is_format) {
    raw_text = raw_text.substr(1, raw_text.length() - 1);
  }
  if (raw_text.size() < 2) {
    throw std::runtime_error("Found string literal with less than 2 characters but expect to find quotes!");
  }
  if (raw_text[0] != '"' || raw_text[raw_text.length()-1] != '"') {
    throw std::runtime_error("Found string literal without leading and ending quotes!");
  }
  data.value = raw_text.substr(1, raw_text.length() - 2);
}

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

// Operator precedence (higher number = higher precedence)
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

//DATA(Expression) {
//  VARIANT(Identifier, Integer, Float, Boolean, String, Unary, Binary, Conditional, Parenthesis) expr;
//};

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
      state.emit_error("Expected literal or identifier in expression.");
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
      state.emit_error("Unary operator expects primary or parenthesized operand!");
      return;
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
      if (!state.expect(TokenType::COLON, " within conditional expression.")) return;
      data.e_false = std::make_unique<ast::Expression>();
      parse(state, data.e_false->data);

    } else {
      expr.expr.emplace<8>();
      auto & data = std::get<8>(expr.expr).data;
      data.expr = std::move(operand);

    }
    if (!state.expect(TokenType::RPAREN, " to end parenthesized expression.")) return;
    
  } else {
    state.emit_error("Expression must be primary, unary, or perenthesized (for binary and conditional)!");
    return;
  }
}

template <>
void Parser::parse<ast::Tag::Path>(ParserState & state, ast::Data<ast::Tag::Path> & path) {
  // Path examples:
  // ?xyz[3:7].abc         - input path
  // prompt_name.field[1]  - prompt path  
  // .field[-1].subfield   - relative path
  
  // Check for input prefix '?'
  if (state.match(TokenType::QUESTION)) {
    path.input = true;
    
    // Expect identifier after '?'
    if (state.expect(TokenType::IDENTIFIER, " after '?' in input path.")) {
      // First step from the input identifier
      path.steps.emplace_back();
      auto & step = path.steps.back().data;
      step.field.data.name = state.previous.text;
      
      // Check for array bounds on this first field
      if (state.match(TokenType::LSQUARE)) {
        step.lower.emplace();
        parse(state, step.lower.value().data);
        if (state.match(TokenType::COLON)) {
          step.upper.emplace();
          if (!state.check(TokenType::RSQUARE)) {
            parse(state, step.upper.value().data);
          }
        }
        if (!state.expect(TokenType::RSQUARE, " to close array access.")) return;
      }
      
      // Continue with rest of path if there's a dot
      if (!state.match(TokenType::DOT)) return;
    } else {
      return; // Error already reported by expect()
    }
  }
  // Check for leading dot (relative path)
  else if (state.match(TokenType::DOT)) {
    path.input = false;
    // No prompt name, starts with a field
  }
  // Otherwise check for prompt name or direct field
  else if (state.check(TokenType::IDENTIFIER)) {
    path.input = false;
    state.advance();
    std::string first_name = state.previous.text;
    
    // Check what follows to determine if it's a prompt name or field
    if (state.match(TokenType::DOT)) {
      // It's a prompt name
      path.prompt.emplace();
      path.prompt.value().data.name = first_name;
    } else {
      // It's just a field, not a prompt prefix
      path.steps.emplace_back();
      auto & step = path.steps.back().data;
      step.field.data.name = first_name;
      
      // Check for array bounds
      if (state.match(TokenType::LSQUARE)) {
        step.lower.emplace();
        parse(state, step.lower.value().data);
        if (state.match(TokenType::COLON)) {
          step.upper.emplace();
          if (!state.check(TokenType::RSQUARE)) {
            parse(state, step.upper.value().data);
          }
        }
        if (!state.expect(TokenType::RSQUARE, " to close array access.")) return;
      }
      
      // Early return if no more path components
      if (!state.match(TokenType::DOT)) return;
    }
  } else {
    // Empty path or invalid start
    return;
  }
  
  // Parse remaining steps (field.field[bounds].field...)
  do {
    if (!state.expect(TokenType::IDENTIFIER, " when parsing path field.")) return;
    
    path.steps.emplace_back();
    auto & step = path.steps.back().data;
    step.field.data.name = state.previous.text;
    
    // Check for array bounds [lower:upper] or [index]
    if (state.match(TokenType::LSQUARE)) {
      step.lower.emplace();
      parse(state, step.lower.value().data);
      
      if (state.match(TokenType::COLON)) {
        step.upper.emplace();
        if (!state.check(TokenType::RSQUARE)) {
          parse(state, step.upper.value().data);
        }
      }
      
      if (!state.expect(TokenType::RSQUARE, " to close array access.")) return;
    }
  } while (state.match(TokenType::DOT));
}

template <>
void Parser::parse<ast::Tag::Import>(ParserState & state, ast::Data<ast::Tag::Import> & import) {
  if (!state.expect(TokenType::STRING_LITERAL, " when parsing import file path.")) return;
  
  std::string raw_text = state.previous.text;
  
  // Reject f-strings for import paths
  if (!raw_text.empty() && (raw_text[0] == 'f' || raw_text[0] == 'F')) {
    state.emit_error("Format strings (f-strings) are not allowed for import paths.");
    return;
  }
  
  // Strip quotes from regular string
  std::string file_path = raw_text;
  if (file_path.length() >= 2 &&
      ((file_path.front() == '"' && file_path.back() == '"') ||
       (file_path.front() == '\'' && file_path.back() == '\''))) {
    file_path = file_path.substr(1, file_path.length() - 2);
  }
  import.file = file_path;
  
  if (!state.expect(TokenType::IMPORT, " after file path in import statement.")) return;
  
  do {
    if (!state.expect(TokenType::IDENTIFIER, " when parsing import target.")) return;
    std::string target = state.previous.text;
    std::string alias = target;
    if (state.match(TokenType::AS)) {
      if (!state.expect(TokenType::IDENTIFIER, " when parsing import alias.")) return;
      alias = state.previous.text;
    }
    import.targets[alias] = target;
  } while (state.match(TokenType::COMMA));
  
  if (!state.expect(TokenType::SEMICOLON, " to end import statement.")) return;
}

template <>
void Parser::parse<ast::Tag::Export>(ParserState & state, ast::Data<ast::Tag::Export> & entry) {
  if (!state.expect(TokenType::IDENTIFIER, " when parsing exported entry point.")) return;
  entry.alias = state.previous.text;
  if (!state.expect(TokenType::IS, " in export statement.")) return;
  if (!state.expect(TokenType::IDENTIFIER, " when parsing export target.")) return;
  entry.target = state.previous.text;
  if (state.match(TokenType::LT)) {
    do {
      if (!state.expect(TokenType::IDENTIFIER, " when parsing export argument name.")) return;
      std::string arg_name = state.previous.text;
      if (!state.expect(TokenType::EQUAL, " when parsing export argument.")) return;
      parse(state, entry.kwargs[arg_name].data);
    } while (state.match(TokenType::COMMA));
    if (!state.expect(TokenType::GT, " to close export arguments.")) return;
  }
  if (!state.expect(TokenType::SEMICOLON, " to end export statement.")) return;
}

#define DEBUG_Parser_Define VERBOSE && 0

template <>
void Parser::parse<ast::Tag::Define>(ParserState & state, ast::Data<ast::Tag::Define> & define) {
#if DEBUG_Parser_Define
    std::cerr << "Parser::parse<ast::Tag::Define>" << std::endl;
#endif
  if (state.match(TokenType::EQUAL)) {
    define.init.emplace();
    parse(state, define.init.value().data);
  }
  if (!state.expect(TokenType::SEMICOLON, " to end define/argument statement.")) return;
}

template <>
void Parser::parse<ast::Tag::Annotate>(ParserState & state, ast::Data<ast::Tag::Annotate> & annotate) {
  if (state.match(TokenType::LBRACE)) {
    // Block form: annotate { path as "description"; ... }
    annotate.single_statement = false;
    
    while (!state.match(TokenType::RBRACE)) {
      annotate.annotations.emplace_back();
      auto & annotation = annotate.annotations.back().data;
      
      // Parse optional path (could be _ for no path)
      if (!(state.match(TokenType::IDENTIFIER) && state.previous.text == "_")) {
        annotation.path.emplace();
        parse(state, annotation.path.value().data);
      }
      
      if (!state.expect(TokenType::AS, " in annotation.")) return;
      if (!state.expect(TokenType::STRING_LITERAL, " for annotation description.")) return;
      
      // Handle both regular and format strings
      std::string raw_text = state.previous.text;
      if (!raw_text.empty() && (raw_text[0] == 'f' || raw_text[0] == 'F')) {
        annotation.description.data.is_format = true;
        // Remove f prefix and quotes
        if (raw_text.length() >= 3) {
          annotation.description.data.value = raw_text.substr(2, raw_text.length() - 3);
        }
      } else {
        annotation.description.data.is_format = false;
        // Remove just quotes
        if (raw_text.length() >= 2 &&
            ((raw_text.front() == '"' && raw_text.back() == '"') ||
             (raw_text.front() == '\'' && raw_text.back() == '\''))) {
          annotation.description.data.value = raw_text.substr(1, raw_text.length() - 2);
        } else {
          annotation.description.data.value = raw_text;
        }
      }
      
      if (!state.expect(TokenType::SEMICOLON, " to end annotation.")) return;
    }
  } else if (state.expect(TokenType::STRING_LITERAL, " when parsing autonomous annotation.")) {
    // Single statement form
    annotate.single_statement = true;
    annotate.annotations.emplace_back();
    auto & annotation = annotate.annotations.back().data;
    
    // Handle both regular and format strings
    std::string raw_text = state.previous.text;
    if (!raw_text.empty() && (raw_text[0] == 'f' || raw_text[0] == 'F')) {
      annotation.description.data.is_format = true;
      if (raw_text.length() >= 3) {
        annotation.description.data.value = raw_text.substr(2, raw_text.length() - 3);
      }
    } else {
      annotation.description.data.is_format = false;
      if (raw_text.length() >= 2 &&
          ((raw_text.front() == '"' && raw_text.back() == '"') ||
           (raw_text.front() == '\'' && raw_text.back() == '\''))) {
        annotation.description.data.value = raw_text.substr(1, raw_text.length() - 2);
      } else {
        annotation.description.data.value = raw_text;
      }
    }
    
    state.expect(TokenType::SEMICOLON, " when ending single statement annotation.");
  }
}

template <>
void Parser::parse<ast::Tag::Search>(ParserState & state, ast::Data<ast::Tag::Search> & search) {
  if (!state.expect(TokenType::LBRACE, " when starting to parse a block of search parameters.")) return;
  while (!state.match(TokenType::RBRACE)) {
    search.params.emplace_back();
    auto & data = search.params.back().data;
    while (state.match(TokenType::IDENTIFIER)) {
      data.locator.emplace_back(state.previous.text);
      if (!state.match(TokenType::DOT)) break;
    }
    if (!state.expect(TokenType::EQUAL, " to set value of search parameter.")) return;
    parse(state, data.value.data);
    if (!state.expect(TokenType::SEMICOLON, " to finish search parameter assignment.")) return;
  }
}

template <>
void Parser::parse<ast::Tag::Field>(ParserState & state, ast::Data<ast::Tag::Field> & field) {
  if (state.match(TokenType::LSQUARE)) {
    field.lower.emplace();
    parse(state, field.lower.value().data);
    
    if (state.match(TokenType::COLON)) {
      field.upper.emplace();
      parse(state, field.upper.value().data);
    }    
    if (!state.expect(TokenType::RSQUARE, " to close array dimension.")) return;
  }
  if (!state.expect(TokenType::IS, " between field name and type.")) return;
  if (state.check(TokenType::LBRACE)) {
    field.type.emplace<1>();
    parse(state, std::get<1>(field.type).data);
  } else {
    field.type.emplace<0>();
    parse(state, std::get<0>(field.type).data);
  }
}

template <>
void Parser::parse<ast::Tag::Struct>(ParserState & state, ast::Data<ast::Tag::Struct> & data) {
  if (!state.expect(TokenType::LBRACE, " when starting to parse struct body.")) return;

  while (!state.match(TokenType::RBRACE)) {
    if (!state.expect(TokenType::IDENTIFIER, " when parsing field name.")) return;
    std::string field_name = state.previous.text;

    data.fields.emplace_back(std::make_unique<ast::Field>());
    auto & field = data.fields.back()->data;
    field.name = field_name;
    parse(state, field);
  }
}

template <>
void Parser::parse<ast::Tag::Channel>(ParserState & state, ast::Data<ast::Tag::Channel> & data) {
  // Channel syntax: channel { to .target from source; to .target call {...}; ... }
  
  if (!state.expect(TokenType::LBRACE, " when starting to parse channel body.")) return;
  
  while (!state.match(TokenType::RBRACE)) {
    // Each link starts with "to"
    if (!state.expect(TokenType::TO, " when starting channel link.")) return;
    
    // Create a new link
    data.links.emplace_back();
    auto & link = data.links.back().data;
    
    // Parse target path
    parse(state, link.target.data);
    
    // Check what follows: "from" for dataflow or "call" for call
    if (state.match(TokenType::FROM)) {
      // Source is a path
      link.source.emplace<0>();  // Path variant
      parse(state, std::get<0>(link.source).data);
      
    } else if (state.match(TokenType::CALL)) {
      // Source is a call
      link.source.emplace<1>();  // Call variant
      auto & call = std::get<1>(link.source).data;
      
      if (!state.expect(TokenType::LBRACE, " to start call block.")) return;
      
      // Parse call components
      while (!state.match(TokenType::RBRACE)) {
        if (state.match(TokenType::ENTRY)) {
          // entry identifier;
          if (!state.expect(TokenType::IDENTIFIER, " for entry name.")) return;
          call.entry.data.name = state.previous.text;
          if (!state.expect(TokenType::SEMICOLON, " after entry declaration.")) return;
          
        } else if (state.match(TokenType::KWARG)) {
          // kwarg name from/map source;
          call.kwargs.emplace_back();
          auto & kwarg = call.kwargs.back().data;
          
          if (!state.expect(TokenType::IDENTIFIER, " for kwarg name.")) return;
          kwarg.name.data.name = state.previous.text;
          
          // Check for "from" or "map"
          if (state.match(TokenType::FROM)) {
            kwarg.mapped = false;
          } else if (state.match(TokenType::MAP)) {
            kwarg.mapped = true;
          } else {
            state.emit_error("Expected 'from' or 'map' after kwarg name.");
            return;
          }
          
          parse(state, kwarg.source.data);
          if (!state.expect(TokenType::SEMICOLON, " after kwarg declaration.")) return;
          
        } else if (state.match(TokenType::BIND)) {
          if (!state.expect(TokenType::IDENTIFIER, " for bind alias.")) return;
          auto & bind_path = call.binds[state.previous.text].data;
          bind_path.input = false;
          bind_path.prompt = std::nullopt;

          if (!state.expect(TokenType::FROM, " for bind source.")) return;
          if (!state.expect(TokenType::IDENTIFIER, " for bind path.")) return;

          bind_path.steps.emplace_back();
          bind_path.steps.back().data.field.data.name = state.previous.text;

          while (state.match(TokenType::DOT)) {
            if (!state.expect(TokenType::IDENTIFIER, " in bind path.")) return;
            bind_path.steps.emplace_back();
            bind_path.steps.back().data.field.data.name = state.previous.text;
          }
          if (!state.expect(TokenType::SEMICOLON, " after bind declaration.")) return;
          
        } else {
          state.emit_error("Unexpected token in call block. Expected 'entry', 'kwarg', or 'bind'.");
          return;
        }
      }
    } else {
      state.emit_error("Expected 'from' or 'call' after channel target.");
      return;
    }
    
    if (!state.expect(TokenType::SEMICOLON, " to end channel link.")) return;
  }
}

template <>
void Parser::parse<ast::Tag::Flow>(ParserState & state, ast::Data<ast::Tag::Flow> & data) {
  // Flow syntax:
  // flow { to prompt_name as "label"; to prompt[index] as "label"; ... }
  // or single: flow to prompt_name as "label";
  
  if (state.match(TokenType::LBRACE)) {
    // Block form
    data.single_statement = false;
    
    while (!state.match(TokenType::RBRACE)) {
      if (!state.expect(TokenType::TO, " when starting flow edge.")) return;
      
      data.edges.emplace_back();
      auto & edge = data.edges.back().data;
      
      // Parse prompt identifier
      if (!state.expect(TokenType::IDENTIFIER, " for flow target prompt.")) return;
      edge.prompt.data.name = state.previous.text;
      
      // Parse label
      if (state.match(TokenType::AS)) {
        if (!state.expect(TokenType::STRING_LITERAL, " for flow edge label.")) return;
        edge.label.emplace();
        clean_raw_string(state.previous.text, edge.label.value().data);
      }
      
      if (!state.expect(TokenType::SEMICOLON, " to end flow edge.")) return;
    }
  } else {
    // Single statement form
    data.single_statement = true;
    data.edges.emplace_back();
    auto & edge = data.edges.back().data;
    
    // Parse prompt identifier
    if (!state.expect(TokenType::IDENTIFIER, " for flow target prompt.")) return;
    edge.prompt.data.name = state.previous.text;

    // Parse label
    if (state.match(TokenType::AS)) {
      if (!state.expect(TokenType::STRING_LITERAL, " for flow edge label.")) return;
      edge.label.emplace();
      clean_raw_string(state.previous.text, edge.label.value().data);
    }
    
    if (!state.expect(TokenType::SEMICOLON, " to end flow statement.")) return;
  }
}

template <>
void Parser::parse<ast::Tag::Return>(ParserState & state, ast::Data<ast::Tag::Return> & data) {
  // Return syntax:
  // return { as "label"; from .path; from .path as identifier; ... }
  
  if (!state.expect(TokenType::LBRACE, " to start return block.")) return;
  
  // Parse optional label first
  if (state.match(TokenType::AS)) {
    if (!state.expect(TokenType::STRING_LITERAL, " for return label.")) return;
    data.label.emplace();
    clean_raw_string(state.previous.text, data.label.value().data);
    if (!state.expect(TokenType::SEMICOLON, " after return label.")) return;
  }
  
  // Parse field mappings
  while (!state.match(TokenType::RBRACE)) {
    if (!state.expect(TokenType::FROM, " when parsing return field.")) return;
    
    data.fields.emplace_back();
    auto & field = data.fields.back().data;
    parse(state, field.field.data);
    if (state.match(TokenType::AS)) {
      if (!state.expect(TokenType::IDENTIFIER, " for return field alias.")) return;
      field.alias.emplace(state.previous.text);
    }
    if (!state.expect(TokenType::SEMICOLON, " to end return field.")) return;
  }
}

template <>
void Parser::parse<ast::Tag::Format>(ParserState & state, ast::Data<ast::Tag::Format> & data) {
  switch (state.current.type) {
    case TokenType::IDENTIFIER: {
      state.advance();
      data.type.emplace<0>();
      auto & type = std::get<0>(data.type).data;
      type.name = state.previous.text;
      break;
    }
    case TokenType::TEXT: {
      state.advance();
      data.type.emplace<1>();
      [[maybe_unused]] auto & type = std::get<1>(data.type).data;
      if (state.match(TokenType::LPAREN)) {
        // TODO how should we represent vocab source???
        if (!state.expect(TokenType::RPAREN, ".")) return;
      }
      break;
    }
    case TokenType::ENUM: {
      state.advance();
      data.type.emplace<2>();
      auto & type = std::get<2>(data.type).data;
      if (!state.expect(TokenType::LPAREN, "")) return;
      if (!state.expect(TokenType::STRING_LITERAL, " when parsing enumerators.")) return;
      type.enumerators.emplace_back(state.previous.text);
      while (!state.match(TokenType::RPAREN)) {
        if (!state.expect(TokenType::COMMA, " when parsing enumerators.")) return;
        if (!state.expect(TokenType::STRING_LITERAL, " when parsing enumerators.")) return;
        type.enumerators.emplace_back(state.previous.text);     
      }
      break;
    }
    case TokenType::REPEAT:
    case TokenType::SELECT: {
      state.advance();
      data.type.emplace<3>();
      auto & type = std::get<3>(data.type).data;
      if (state.current.type == TokenType::REPEAT) {
        type.mode = ast::ChoiceKind::Repeat;
      } else {
        type.mode = ast::ChoiceKind::Select;
      }

      if (!state.expect(TokenType::LPAREN, " when parsing a choice format.")) return;
      parse(state, type.source.data);
      if (!state.expect(TokenType::RPAREN, " at the end of a choice format.")) return;
        
      break;
    }
    default: {
      std::ostringstream oss;
      oss << "Unexpected token `" << token_type_name(state.current.type) << "` while parsing top level statements of a Record.";
      state.emit_error(oss.str());
    }
  }
  if (state.match(TokenType::LT)) {
    do {
      auto start = state.current.location;
      if (!state.expect(TokenType::IDENTIFIER, " when parsing format argument name.")) return;
      std::string arg_name = state.previous.text;
      if (!state.expect(TokenType::EQUAL, " when parsing format argument.")) return;
      parse_with_location(state, data.kwargs[arg_name], start);
    } while (state.match(TokenType::COMMA));
    if (!state.expect(TokenType::GT, " to close format arguments.")) return;
  }
  state.expect(TokenType::SEMICOLON, " to finish format statement");
}

template <>
void Parser::parse<ast::Tag::Record>(ParserState & state, ast::Data<ast::Tag::Record> & data) {
  if (!state.expect(TokenType::LBRACE, " when starting to parse a Record.")) return;
  while (!state.match(TokenType::RBRACE)) {
    auto start = state.current.location;
    switch (state.current.type) {
      case TokenType::DEFINE:
      case TokenType::ARGUMENT: {
        bool is_argument = (state.current.type == TokenType::ARGUMENT);
        state.advance();
        if (!state.expect(TokenType::IDENTIFIER, " when determining defined identifier.")) break;
        auto name = state.previous.text;
        auto & node_ = data.defines[name];
        auto & data_ = node_.data;
        data_.name = name;
        data_.argument = is_argument;
        parse_with_location(state, node_, start);
        break;
      }
      case TokenType::ANNOTATE: {
        state.advance();
        data.annotate.emplace();
        parse_with_location(state, data.annotate.value(), start);
        break;
      }
      case TokenType::SEARCH: {
        state.advance();
        data.search.emplace();
        parse_with_location(state, data.search.value(), start);
        break;
      }
      case TokenType::IS: {
        state.advance();
        if (state.check(TokenType::LBRACE)) {
          data.record.emplace<0>();
          parse_with_location(state, std::get<0>(data.record), start);
        } else {
          data.record.emplace<1>();
          parse_with_location(state, std::get<1>(data.record), start);
        }
        break;
      }
      default: {
        std::ostringstream oss;
        oss << "Unexpected token `" << token_type_name(state.current.type) << "` while parsing top level statements of a Record.";
        state.emit_error(oss.str());
      }
    }
  }
}

template <>
void Parser::parse<ast::Tag::Prompt>(ParserState & state, ast::Data<ast::Tag::Prompt> & data) {
  if (!state.expect(TokenType::LBRACE, " when starting to parse a Prompt.")) return;
  while (!state.match(TokenType::RBRACE)) {
    auto start = state.current.location;
    switch (state.current.type) {
      case TokenType::DEFINE:
      case TokenType::ARGUMENT: {
        bool is_argument = (state.current.type == TokenType::ARGUMENT);
        state.advance();
        if (!state.expect(TokenType::IDENTIFIER, " when determining defined identifier.")) break;
        auto name = state.previous.text;
        auto & node_ = data.defines[name];
        auto & data_ = node_.data;
        data_.name = name;
        data_.argument = is_argument;
        parse_with_location(state, node_, start);
        break;
      }
      case TokenType::ANNOTATE: {
        state.advance();
        data.annotate.emplace();
        parse_with_location(state, data.annotate.value(), start);
        break;
      }
      case TokenType::SEARCH: {
        state.advance();
        data.search.emplace();
        parse_with_location(state, data.search.value(), start);
        break;
      }
      case TokenType::IS: {
        state.advance();
        parse_with_location(state, data.fields, start);
        break;
      }
      case TokenType::CHANNEL: {
        state.advance();
        data.channel.emplace();
        parse_with_location(state, data.channel.value(), start);
        break;
      }
      case TokenType::FLOW: {
        state.advance();
        data.flow.emplace();
        parse_with_location(state, data.flow.value(), start);
        break;
      }
      case TokenType::RETURN: {
        state.advance();
        data.retstmt.emplace();
        parse_with_location(state, data.retstmt.value(), start);
        break;
      }
      default: {
        std::ostringstream oss;
        oss << "Unexpected token `" << token_type_name(state.current.type) << "` while parsing top level statements of a Prompt.";
        state.emit_error(oss.str());
      }
    }
  }
}

template <>
void Parser::parse<ast::Tag::Program>(ParserState & state, ast::Data<ast::Tag::Program> & data) {
  while (!state.match(TokenType::END_OF_FILE)) {
    auto start = state.current.location;
    switch (state.current.type) {
      case TokenType::FROM: {
        state.advance();
        data.imports.emplace_back();
        parse_with_location(state, data.imports.back(), start);
        break;
      }
      case TokenType::EXPORT: {
        state.advance();
        data.exports.emplace_back();
        parse_with_location(state, data.exports.back(), start);
        break;
      }
      case TokenType::DEFINE: {
        state.advance();
        if (!state.expect(TokenType::IDENTIFIER, " when determining defined identifier.")) break;
        auto name = state.previous.text;
        auto & node_ = data.defines[name];
        auto & data_ = node_.data;
        data_.name = name;
        parse_with_location(state, node_, start);
        break;
      }
      case TokenType::ANNOTATE: {
        state.advance();
        data.annotate.emplace();
        parse_with_location(state, data.annotate.value(), start);
        break;
      }
      case TokenType::SEARCH: {
        state.advance();
        data.search.emplace();
        parse_with_location(state, data.search.value(), start);
        break;
      }
      case TokenType::RECORD: {
        state.advance();
        if (!state.expect(TokenType::IDENTIFIER, " when parsing name of a Record.")) break;
        auto name = state.previous.text;
        auto & node_ = data.records[name];
        auto & data_ = node_.data;
        data_.name = name;
        parse_with_location(state, node_, start);
        break;
      }
      case TokenType::PROMPT: {
        state.advance();
        if (!state.expect(TokenType::IDENTIFIER, " when parsing name of a Prompt.")) break;
        auto name = state.previous.text;
        auto & node_ = data.prompts[name];
        auto & data_ = node_.data;
        data_.name = name;
        parse_with_location(state, node_, start);
        break;
      }
      default: {
        std::ostringstream oss;
        oss << "Unexpected token `" << token_type_name(state.current.type) << "` while parsing statement of Program.";
        state.emit_error(oss.str());
      }
    }
  }
}

void Parser::parse(int fid, std::string const & name, std::string const & source) {
  ParserState state(fid, source, diagnostics);
  programs.emplace(name, name);
  parse<ast::Tag::Program>(state, programs[name].data);
}

Parser::file_to_program_map_t const & Parser::get() const {
    return programs;
}

}
