%top{
#include "autocog/compiler/stl/token.hxx"
#include "autocog/compiler/stl/location.hxx"
#include <string>
%}

%class{
private:
    int line_number = 1;
    int column_number = 1;
    int last_column = 1;
    unsigned current_offset = 0;
    
    void update_location() {
        last_column = column_number;
        column_number += size();
        current_offset += size();
    }
    
    void newline() {
        line_number++;
        last_column = column_number;
        column_number = 1;
        current_offset++;
    }
    
public:
    SourceLocation current_location() const {
        return {line_number, last_column, current_offset};
    }

    Token advance() {
        TokenType tok = (TokenType)(this->lex());
        Token token{ tok, this->str(), this->current_location() };
        return token;
    }
%}

%option c++ noyywrap
%option lexer=Lexer
%option namespace=autocog::compiler::stl

%%

\n                      { newline(); }
[ \t\r]+                { update_location(); }

"//"[^\n]*              { update_location(); /* line comment */ }
"/*"([^*]|\*[^/])*"*/"  { 
    for (size_t i = 0; i < size(); ++i) {
        if (text()[i] == '\n') newline();
        else column_number++;
    }
}

// Keywords
"define"                { update_location(); return (int)TokenType::DEFINE; }
"argument"              { update_location(); return (int)TokenType::ARGUMENT; }
"record"                { update_location(); return (int)TokenType::RECORD; }
"import"                { update_location(); return (int)TokenType::IMPORT; }
"export"                { update_location(); return (int)TokenType::EXPORT; }
"prompt"                { update_location(); return (int)TokenType::PROMPT; }
"channel"               { update_location(); return (int)TokenType::CHANNEL; }
"flow"                  { update_location(); return (int)TokenType::FLOW; }
"entry"                 { update_location(); return (int)TokenType::ENTRY; }
"return"                { update_location(); return (int)TokenType::RETURN; }
"annotate"              { update_location(); return (int)TokenType::ANNOTATE; }
"to"                    { update_location(); return (int)TokenType::TO; }
"from"                  { update_location(); return (int)TokenType::FROM; }
"call"                  { update_location(); return (int)TokenType::CALL; }
"extern"                { update_location(); return (int)TokenType::EXTERN; }
"kwarg"                 { update_location(); return (int)TokenType::KWARG; }
"map"                   { update_location(); return (int)TokenType::MAP; }
"bind"                  { update_location(); return (int)TokenType::BIND; }
"as"                    { update_location(); return (int)TokenType::AS; }
"is"                    { update_location(); return (int)TokenType::IS; }
"search"                { update_location(); return (int)TokenType::SEARCH; }
"text"                  { update_location(); return (int)TokenType::TEXT; }
"select"                { update_location(); return (int)TokenType::SELECT; }
"repeat"                { update_location(); return (int)TokenType::REPEAT; }
"enum"                  { update_location(); return (int)TokenType::ENUM; }
"true"                  { update_location(); return (int)TokenType::BOOLEAN_LITERAL; }
"false"                 { update_location(); return (int)TokenType::BOOLEAN_LITERAL; }

// Identifiers and literals  
[a-zA-Z_][a-zA-Z0-9_]*  { update_location(); return (int)TokenType::IDENTIFIER; }
[0-9]+                  { update_location(); return (int)TokenType::INTEGER_LITERAL; }
[0-9]+\.[0-9]+          { update_location(); return (int)TokenType::FLOAT_LITERAL; }

[fF]\"([^"\\]|\\.)*\"   { update_location(); return (int)TokenType::STRING_LITERAL; }
[fF]'([^'\\]|\\.)*'     { update_location(); return (int)TokenType::STRING_LITERAL; }
\"([^"\\]|\\.)*\"       { update_location(); return (int)TokenType::STRING_LITERAL; }
'([^'\\]|\\.)*'         { update_location(); return (int)TokenType::STRING_LITERAL; }

// Operators and delimiters
// IMPORTANT: Multi-character operators must come before single-character ones
"<="                    { update_location(); return (int)TokenType::LTEQ; }
">="                    { update_location(); return (int)TokenType::GTEQ; }
"=="                    { update_location(); return (int)TokenType::EQEQ; }
"!="                    { update_location(); return (int)TokenType::BANGEQ; }
"&&"                    { update_location(); return (int)TokenType::AMPAMP; }
"||"                    { update_location(); return (int)TokenType::PIPEPIPE; }

"{"                     { update_location(); return (int)TokenType::LBRACE; }
"}"                     { update_location(); return (int)TokenType::RBRACE; }
"["                     { update_location(); return (int)TokenType::LSQUARE; }
"]"                     { update_location(); return (int)TokenType::RSQUARE; }
"("                     { update_location(); return (int)TokenType::LPAREN; }
")"                     { update_location(); return (int)TokenType::RPAREN; }
";"                     { update_location(); return (int)TokenType::SEMICOLON; }
":"                     { update_location(); return (int)TokenType::COLON; }
","                     { update_location(); return (int)TokenType::COMMA; }
"."                     { update_location(); return (int)TokenType::DOT; }
"="                     { update_location(); return (int)TokenType::EQUAL; }
"+"                     { update_location(); return (int)TokenType::PLUS; }
"-"                     { update_location(); return (int)TokenType::MINUS; }
"*"                     { update_location(); return (int)TokenType::STAR; }
"/"                     { update_location(); return (int)TokenType::SLASH; }
"%"                     { update_location(); return (int)TokenType::PERCENT; }
"<"                     { update_location(); return (int)TokenType::LT; }
">"                     { update_location(); return (int)TokenType::GT; }
"!"                     { update_location(); return (int)TokenType::BANG; }
"?"                     { update_location(); return (int)TokenType::QUESTION; }

.                       { 
    update_location(); 
    return (int)TokenType::ERROR; 
}

<<EOF>>                 { return (int)TokenType::END_OF_FILE; }

%%
