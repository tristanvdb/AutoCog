#ifndef AUTOCOG_PARSER_STL_TOKEN_HXX
#define AUTOCOG_PARSER_STL_TOKEN_HXX

#include <string>
#include <ostream>

namespace autocog {
namespace parser {

// Token types
enum class TokenType : int {
    // Keywords
    DEFINE,
    ARGUMENT,
    FORMAT,
    STRUCT,
    PROMPT,
    CHANNEL,
    FLOW,
    RETURN,
    ANNOTATE,
    TO,
    FROM,
    CALL,
    EXTERN,
    ENTRY,
    KWARG,
    MAP,
    BIND,
    AS,
    IS,
    SEARCH,
    TEXT,
    SELECT,
    REPEAT,
    
    // Identifiers and literals
    IDENTIFIER,
    STRING_LITERAL,
    INTEGER_LITERAL,
    FLOAT_LITERAL,
    BOOLEAN_LITERAL,
    
    // Operators and delimiters
    LBRACE,      // {
    RBRACE,      // }
    LSQUARE,     // [
    RSQUARE,     // ]
    LPAREN,      // (
    RPAREN,      // )
    SEMICOLON,   // ;
    COLON,       // :
    COMMA,       // ,
    DOT,         // .
    EQUAL,       // =
    PLUS,        // +
    MINUS,       // -
    STAR,        // *
    SLASH,       // /
    
    // Special
    ERROR,
    END_OF_FILE
};

// Helper function to get token type name
const char* token_type_name(TokenType type);

} // namespace parser
} // namespace autocog

#endif // AUTOCOG_PARSER_STL_TOKEN_HXX
