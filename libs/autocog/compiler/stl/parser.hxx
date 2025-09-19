#ifndef AUTOCOG_COMPILER_STL_PARSER_HXX
#define AUTOCOG_COMPILER_STL_PARSER_HXX

#include "autocog/compiler/stl/token.hxx"
#include "autocog/compiler/stl/ast.hxx"

#include "autocog_compiler_stl_lexer.hxx" //< Generated file

#include <memory>
#include <queue>
#include <list>
#include <fstream>
#include <sstream>

namespace autocog::compiler::stl {

struct ParseError : std::exception {
  std::string message;
  SourceLocation location;
  
  ParseError(std::string msg, SourceLocation loc);
  
  const char * what() const noexcept override;
};

class Lexer;
struct Diagnostic;

struct ParserState {
  std::istringstream stream;
  Lexer lexer;

  Token previous;
  Token current;

  ParserState(std::string const & source_);
  ParserState(int fid, std::string const & source_);

  void advance();

  bool check(TokenType type);
  bool match(TokenType type);
  void expect(TokenType type, std::string context);

  void throw_error(std::string msg);
};

class Parser {
  private:
    std::list<std::string> const & search_paths;
    std::list<Diagnostic> & diagnostics;
    std::unordered_map<std::string, int> & fileids;
    std::list<ast::Program> & programs;
    std::queue<std::string> queue;

  private:
    void parse(int fid, std::string const & filename, std::string const & source);

    template <ast::Tag tag>
    static void parse(ParserState & state, ast::Data<tag> & data);

    template <ast::Tag tag>
    static void parse_with_location(ParserState & state, ast::Node<tag> & node, std::optional<SourceLocation> start/* = std::nullopt*/) {
      SourceLocation start_loc{start?start.value():state.current.location};
      parse(state, node.data);
      node.location.emplace(SourceRange{start_loc, state.current.location});
    }

    /// For testing purpose
    template <ast::Tag tag>
    static bool parse_fragment(std::string const & code);

  public:
    Parser(
      std::list<Diagnostic> &,
      std::unordered_map<std::string, int> &,
      std::list<std::string> const &,
      std::list<ast::Program> &,
      std::list<std::string> const &
    );

    void parse();

    /// For testing purpose
    static bool parse_fragment(std::string const & tag, std::string const & code);
};

template <ast::Tag tag>
bool Parser::parse_fragment(
  std::string const & code
) {
  ParserState state(code);
  ast::Data<tag> data;
  parse(state, data);
  return state.check(TokenType::END_OF_FILE);
}

void clean_raw_string(std::string raw_text, ast::Data<ast::Tag::String> & data);

template <> void Parser::parse<ast::Tag::Annotate>   (ParserState & state, ast::Data<ast::Tag::Annotate>   &);
template <> void Parser::parse<ast::Tag::Call>       (ParserState & state, ast::Data<ast::Tag::Call>       &);
template <> void Parser::parse<ast::Tag::Channel>    (ParserState & state, ast::Data<ast::Tag::Channel>    &);
template <> void Parser::parse<ast::Tag::Define>     (ParserState & state, ast::Data<ast::Tag::Define>     &);
template <> void Parser::parse<ast::Tag::Export>     (ParserState & state, ast::Data<ast::Tag::Export>     &);
template <> void Parser::parse<ast::Tag::Expression> (ParserState & state, ast::Data<ast::Tag::Expression> &);
template <> void Parser::parse<ast::Tag::Field>      (ParserState & state, ast::Data<ast::Tag::Field>      &);
template <> void Parser::parse<ast::Tag::FieldRef>   (ParserState & state, ast::Data<ast::Tag::FieldRef>   &);
template <> void Parser::parse<ast::Tag::Flow>       (ParserState & state, ast::Data<ast::Tag::Flow>       &);
template <> void Parser::parse<ast::Tag::Format>     (ParserState & state, ast::Data<ast::Tag::Format>     &);
template <> void Parser::parse<ast::Tag::Import>     (ParserState & state, ast::Data<ast::Tag::Import>     &);
template <> void Parser::parse<ast::Tag::Kwarg>      (ParserState & state, ast::Data<ast::Tag::Kwarg>      &);
template <> void Parser::parse<ast::Tag::Link>       (ParserState & state, ast::Data<ast::Tag::Link>       &);
template <> void Parser::parse<ast::Tag::Path>       (ParserState & state, ast::Data<ast::Tag::Path>       &);
template <> void Parser::parse<ast::Tag::Program>    (ParserState & state, ast::Data<ast::Tag::Program>    &);
template <> void Parser::parse<ast::Tag::Prompt>     (ParserState & state, ast::Data<ast::Tag::Prompt>     &);
template <> void Parser::parse<ast::Tag::PromptRef>  (ParserState & state, ast::Data<ast::Tag::PromptRef>  &);
template <> void Parser::parse<ast::Tag::Record>     (ParserState & state, ast::Data<ast::Tag::Record>     &);
template <> void Parser::parse<ast::Tag::Return>     (ParserState & state, ast::Data<ast::Tag::Return>     &);
template <> void Parser::parse<ast::Tag::Search>     (ParserState & state, ast::Data<ast::Tag::Search>     &);
template <> void Parser::parse<ast::Tag::Struct>     (ParserState & state, ast::Data<ast::Tag::Struct>     &);

template <> void Parser::parse<ast::Tag::Bind>(ParserState & state, ast::Data<ast::Tag::Bind> &);
template <> void Parser::parse<ast::Tag::Ravel>(ParserState & state, ast::Data<ast::Tag::Ravel> &);
template <> void Parser::parse<ast::Tag::Mapped>(ParserState & state, ast::Data<ast::Tag::Mapped> &);
template <> void Parser::parse<ast::Tag::Wrap>(ParserState & state, ast::Data<ast::Tag::Wrap> &);
template <> void Parser::parse<ast::Tag::Prune>(ParserState & state, ast::Data<ast::Tag::Prune> &);

template <> void Parser::parse<ast::Tag::Edge>(ParserState & state, ast::Data<ast::Tag::Edge> &);

template <> void Parser::parse<ast::Tag::Retfield>(ParserState & state, ast::Data<ast::Tag::Retfield> &);

template <> void Parser::parse<ast::Tag::Text>(ParserState & state, ast::Data<ast::Tag::Text> &);
template <> void Parser::parse<ast::Tag::Enum>(ParserState & state, ast::Data<ast::Tag::Enum> &);
template <> void Parser::parse<ast::Tag::Choice>(ParserState & state, ast::Data<ast::Tag::Choice> &);

template <> void Parser::parse<ast::Tag::Annotation>(ParserState & state, ast::Data<ast::Tag::Annotation> &);
template <> void Parser::parse<ast::Tag::Param>(ParserState & state, ast::Data<ast::Tag::Param> &);


}

#endif // AUTOCOG_COMPILER_STL_PARSER_HXX
