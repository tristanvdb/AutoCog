#ifndef AUTOCOG_COMPILER_STL_TOKEN_HXX
#define AUTOCOG_COMPILER_STL_TOKEN_HXX

#include "autocog/compiler/stl/location.hxx"

#include <string>
#include <ostream>

namespace autocog::compiler::stl {

// Token types
enum class TokenType : int {
    NOT_A_VALID_TOKEN,
    // Keywords
    DEFINE,
    ARGUMENT,
    RECORD,
    IMPORT,
    EXPORT,
    PROMPT,
    CHANNEL,
    FLOW,
    ENTRY,
    RETURN,
    ANNOTATE,
    TO,
    FROM,
    CALL,
    EXTERN,
    KWARG,
    MAP,
    BIND,
    AS,
    IS,
    SEARCH,
    TEXT,
    SELECT,
    REPEAT,
    ENUM,
    
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
    LT,          // <
    GT,          // >
    
        // Comparison operators
    LTEQ,        // <=
    GTEQ,        // >=
    EQEQ,        // ==
    BANGEQ,      // !=
    
    // Logical operators
    AMPAMP,      // &&
    PIPEPIPE,    // ||
    BANG,        // !
    
    // Other operators
    PERCENT,     // %
    QUESTION,    // ?
    
    // Special
    ERROR,
    END_OF_FILE
};

// Token structure
struct Token {
    TokenType      type{TokenType::NOT_A_VALID_TOKEN};
    std::string    text{""};
    SourceLocation location{-1,-1,0};
};

// Helper function to get token type name
const char* token_type_name(TokenType type);

}

#endif // AUTOCOG_COMPILER_STL_TOKEN_HXX
