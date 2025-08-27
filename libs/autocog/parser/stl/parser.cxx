
#include "autocog/parser/stl/diagnostic.hxx"
#include "autocog/parser/stl/parser.hxx"
#include "autocog/parser/stl/lexer.hxx"

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
            oss << "Cannot read file: " << filepath;
            diagnostics.emplace_back(DiagnosticLevel::Error, oss.str());
        } else {
            std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            parse(filepath, source);
            file.close();
            programs[filepath].exec.queue_imports(queue);
        }
    }
}

void Parser::parse(std::string const & name, std::string const & source) {
    auto & program = programs[name];
    std::istringstream stream(source);

    Lexer lexer(stream);
    TokenType current = (TokenType)(lexer.lex());
    switch (current) {
//      case TokenType::PROMPT:
//        throw std::runtime_error("Not implemented yet : Parser::parse<IrTag::Prompt>()"); // TODO
//        break;
      default:
        std::ostringstream oss;
        oss << "Unexpected token " << token_type_name(current);
        auto loc = lexer.current_location();
        auto line = get_line(source, loc.line);
        diagnostics.emplace_back(DiagnosticLevel::Error, oss.str(), line, loc);
    }
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
