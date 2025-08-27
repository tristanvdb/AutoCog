%top{
#include "autocog/parser/stl/token.hxx"
#include "autocog/parser/stl/location.hxx"
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
    autocog::parser::SourceLocation current_location() const {
        return {line_number, last_column, current_offset};
    }
%}

%option c++ noyywrap
%option lexer=Lexer
%option namespace=autocog::parser

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
"format"                { update_location(); return (int)TokenType::FORMAT; }
"struct"                { update_location(); return (int)TokenType::STRUCT; }
"prompt"                { update_location(); return (int)TokenType::PROMPT; }
"channel"               { update_location(); return (int)TokenType::CHANNEL; }
"flow"                  { update_location(); return (int)TokenType::FLOW; }
"return"                { update_location(); return (int)TokenType::RETURN; }
"annotate"              { update_location(); return (int)TokenType::ANNOTATE; }
"to"                    { update_location(); return (int)TokenType::TO; }
"from"                  { update_location(); return (int)TokenType::FROM; }
"call"                  { update_location(); return (int)TokenType::CALL; }
"extern"                { update_location(); return (int)TokenType::EXTERN; }
"entry"                 { update_location(); return (int)TokenType::ENTRY; }
"kwarg"                 { update_location(); return (int)TokenType::KWARG; }
"map"                   { update_location(); return (int)TokenType::MAP; }
"bind"                  { update_location(); return (int)TokenType::BIND; }
"as"                    { update_location(); return (int)TokenType::AS; }
"is"                    { update_location(); return (int)TokenType::IS; }
"search"                { update_location(); return (int)TokenType::SEARCH; }
"text"                  { update_location(); return (int)TokenType::TEXT; }
"select"                { update_location(); return (int)TokenType::SELECT; }
"repeat"                { update_location(); return (int)TokenType::REPEAT; }
"true"                  { update_location(); return (int)TokenType::BOOLEAN_LITERAL; }
"false"                 { update_location(); return (int)TokenType::BOOLEAN_LITERAL; }

// Identifiers and literals  
[a-zA-Z_][a-zA-Z0-9_]*  { update_location(); return (int)TokenType::IDENTIFIER; }
[0-9]+                  { update_location(); return (int)TokenType::INTEGER_LITERAL; }
[0-9]+\.[0-9]+          { update_location(); return (int)TokenType::FLOAT_LITERAL; }
\"([^"\\]|\\.)*\"       { update_location(); return (int)TokenType::STRING_LITERAL; }
'([^'\\]|\\.)*'         { update_location(); return (int)TokenType::STRING_LITERAL; }

// Operators and delimiters
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

.                       { 
    update_location(); 
    return (int)TokenType::ERROR; 
}

<<EOF>>                 { return (int)TokenType::END_OF_FILE; }

%%
