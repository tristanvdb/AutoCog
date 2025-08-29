
#include "autocog/parser/stl/parser.hxx"
#include "autocog/parser/stl/diagnostic.hxx"

#include <stdexcept>

namespace autocog::parser {

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
  std::string const & filename_,
  std::string const & source_,
  std::list<Diagnostic> & diagnostics_
) :
  filename(filename_), 
  source(source_),
  diagnostics(diagnostics_),
  stream(source),
  lexer(stream),
  previous(),
  current(lexer.advance()),
  error(false)
{}

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
  error = true;
}

Parser::Parser(std::list<Diagnostic> & diagnostics_) :
  queue(),
  diagnostics(diagnostics_),
  programs()
{}

void Parser::parse() {
  while (!queue.empty()) {
    std::string filepath = queue.front();
    queue.pop();

    std::ifstream file(filepath); // TODO file lookup in 'search_paths'
    if (!file) {
      std::ostringstream oss;
      oss << "Cannot read file: `" << filepath << "`";
      diagnostics.emplace_back(DiagnosticLevel::Error, oss.str());
    } else {
      std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      parse(filepath, source);
      file.close();
      programs[filepath].exec.queue_imports(queue);
    }
  }
}

template <>
void Parser::parse<IrTag::Import>(ParserState & state, IrData<IrTag::Import> & import) {
  throw std::runtime_error("NIY: Parser::parse<IrTag::Import>()"); // TODO
}

template <>
void Parser::parse<IrTag::Export>(ParserState & state, IrData<IrTag::Export> & entry) {
  throw std::runtime_error("NIY: Parser::parse<IrTag::Export>()"); // TODO
}

template <>
void Parser::parse<IrTag::Define>(ParserState & state, IrData<IrTag::Define> & define) {
  throw std::runtime_error("NIY: Parser::parse<IrTag::Define>()"); // TODO
}

template <>
void Parser::parse<IrTag::Expression>(ParserState & state, IrData<IrTag::Expression> & expression) {
  throw std::runtime_error("NIY: Parser::parse<IrTag::Expression>()"); // TODO
}

template <>
void Parser::parse<IrTag::Path>(ParserState & state, IrData<IrTag::Path> & path) {
  throw std::runtime_error("NIY: Parser::parse<IrTag::Path>()"); // TODO
}

template <>
void Parser::parse<IrTag::Annotate>(ParserState & state, IrData<IrTag::Annotate> & annotate) {
  if (state.match(TokenType::LBRACE)) {
    annotate.single_statement = false;
    throw std::runtime_error("NIY: Parser::parse<IrTag::Annotate>() with body"); // TODO
  } else if (state.expect(TokenType::STRING_LITERAL, " when parsing autonmous annotation.")) {
    annotate.single_statement = true;
    annotate.annotations.emplace_back(std::nullopt, state.previous.text);
    state.expect(TokenType::SEMICOLON, " when ending single statement annotation.");
  }
}

template <>
void Parser::parse<IrTag::Search>(ParserState & state, IrData<IrTag::Search> & search) {
  if (!state.expect(TokenType::LBRACE, " when starting to parse a block of search parameters.")) return;
  while (!state.error && !state.match(TokenType::RBRACE)) {
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
void Parser::parse<IrTag::Struct>(ParserState & state, IrData<IrTag::Struct> & data) {
  throw std::runtime_error("NIY: Parser::parse<IrTag::Struct>()"); // TODO
}

template <>
void Parser::parse<IrTag::Channel>(ParserState & state, IrData<IrTag::Channel> & data) {
  throw std::runtime_error("NIY: Parser::parse<IrTag::Channel>()"); // TODO
}

template <>
void Parser::parse<IrTag::Flow>(ParserState & state, IrData<IrTag::Flow> & data) {
  throw std::runtime_error("NIY: Parser::parse<IrTag::Flow>()"); // TODO
}

template <>
void Parser::parse<IrTag::Return>(ParserState & state, IrData<IrTag::Return> & data) {
  throw std::runtime_error("NIY: Parser::parse<IrTag::Return>()"); // TODO
}

template <>
void Parser::parse<IrTag::Format>(ParserState & state, IrData<IrTag::Format> & data) {
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
      auto & type = std::get<1>(data.type).data;
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
      while (!state.error && !state.match(TokenType::RPAREN)) {
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
        type.mode = ChoiceKind::Repeat;
      } else {
        type.mode = ChoiceKind::Select;
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
    // TODO parse KWARGS
    state.expect(TokenType::GT, ".");
  }
  state.expect(TokenType::SEMICOLON, " to finish format statement");
}

template <>
void Parser::parse<IrTag::Record>(ParserState & state, IrData<IrTag::Record> & data) {
  if (!state.expect(TokenType::LBRACE, " when starting to parse a Record.")) return;
  while (!state.error && !state.match(TokenType::RBRACE)) {
    switch (state.current.type) {
      case TokenType::DEFINE:
      case TokenType::ARGUMENT: {
        state.advance();
        if (!state.expect(TokenType::IDENTIFIER, " when determining defined identifier.")) break;
        auto name = state.previous.text;
        auto & data_ = data.defines[name].data;
        data_.name = name;
        data_.argument = (state.current.type == TokenType::ARGUMENT);
        parse(state, data_);
        break;
      }
      case TokenType::ANNOTATE: {
        state.advance();
        data.annotate.emplace();
        parse(state, data.annotate.value().data);
        break;
      }
      case TokenType::SEARCH: {
        state.advance();
        data.search.emplace();
        parse(state, data.search.value().data);
        break;
      }
      case TokenType::IS: {
        state.advance();
        if (state.check(TokenType::LBRACE)) {
          data.record.emplace<0>();
          parse(state, std::get<0>(data.record).data);
        } else {
          data.record.emplace<1>();
          parse(state, std::get<1>(data.record).data);
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
void Parser::parse<IrTag::Prompt>(ParserState & state, IrData<IrTag::Prompt> & data) {
  if (!state.expect(TokenType::LBRACE, " when starting to parse a Prompt.")) return;
  while (!state.error && !state.match(TokenType::RBRACE)) {
    switch (state.current.type) {
      case TokenType::DEFINE:
      case TokenType::ARGUMENT: {
        state.advance();
        if (!state.expect(TokenType::IDENTIFIER, " when determining defined identifier.")) break;
        auto name = state.previous.text;
        auto & data_ = data.defines[name].data;
        data_.name = name;
        data_.argument = (state.current.type == TokenType::ARGUMENT);
        parse(state, data_);
        break;
      }
      case TokenType::ANNOTATE: {
        state.advance();
        data.annotate.emplace();
        parse(state, data.annotate.value().data);
        break;
      }
      case TokenType::SEARCH: {
        state.advance();
        data.search.emplace();
        parse(state, data.search.value().data);
        break;
      }
      case TokenType::IS: {
        state.advance();
        parse(state, data.fields.data);
        break;
      }
      case TokenType::CHANNEL: {
        state.advance();
        data.channel.emplace();
        parse(state, data.channel.value().data);
        break;
      }
      case TokenType::FLOW: {
        state.advance();
        data.flow.emplace();
        parse(state, data.flow.value().data);
        break;
      }
      case TokenType::RETURN: {
        state.advance();
        data.retstmt.emplace();
        parse(state, data.retstmt.value().data);
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
void Parser::parse<IrTag::Program>(ParserState & state, IrData<IrTag::Program> & data) {
  while (!state.error && !state.match(TokenType::END_OF_FILE)) {
    switch (state.current.type) {
      case TokenType::FROM: {
        state.advance();
        data.imports.emplace_back();
        parse(state, data.imports.back().data);
        break;
      }
      case TokenType::EXPORT: {
        state.advance();
        if (!state.expect(TokenType::IDENTIFIER, " when parsing aliased entry point.")) break;
        auto alias = state.previous.text;
        auto & data_ = data.exports[alias].data;
        data_.alias = alias;
        parse(state, data_);
        break;
      }
      case TokenType::DEFINE: {
        state.advance();
        if (!state.expect(TokenType::IDENTIFIER, " when determining defined identifier.")) break;
        auto name = state.previous.text;
        auto & data_ = data.defines[name].data;
        data_.name = name;
        parse(state, data_);
        break;
      }
      case TokenType::ANNOTATE: {
        state.advance();
        data.annotate.emplace();
        parse(state, data.annotate.value().data);
        break;
      }
      case TokenType::SEARCH: {
        state.advance();
        data.search.emplace();
        parse(state, data.search.value().data);
        break;
      }
      case TokenType::RECORD: {
        state.advance();
        if (!state.expect(TokenType::IDENTIFIER, " when parsing name of a Record.")) break;
        auto name = state.previous.text;
        auto & data_ = data.records[name].data;
        data_.name = name;
        parse(state, data_);
        break;
      }
      case TokenType::PROMPT: {
        state.advance();
        if (!state.expect(TokenType::IDENTIFIER, " when parsing name of a Prompt.")) break;
        auto name = state.previous.text;
        auto & data_ = data.prompts[name].data;
        data_.name = name;
        parse(state, data_);
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

void Parser::parse(std::string const & name, std::string const & source) {
  ParserState state(name, source, diagnostics);
  parse<IrTag::Program>(state, programs[name].data);
}

MAPPED(Program) const & Parser::get() const {
    return programs;
}

bool Parser::has_prompt(std::string const & filename, std::string const & objname) const {
    auto & program = programs.at(filename);
    return program.data.prompts.find(objname) != program.data.prompts.end();
}

NODE(Prompt) const & Parser::get_prompt(std::string const & filename, std::string const & objname) const {
    auto & program = programs.at(filename);
    return program.data.prompts.at(objname);
}

bool Parser::has_record(std::string const & filename, std::string const & objname) const {
    auto & program = programs.at(filename);
    return program.data.records.find(objname) != program.data.records.end();
}

NODE(Record) const & Parser::get_record(std::string const & filename, std::string const & objname) const {
    auto & program = programs.at(filename);
    return program.data.records.at(objname);
}

}
