
#include "autocog/parser/stl/token.hxx"

namespace autocog::parser {

// TODO move to token.cxx
const char * token_type_name(TokenType type) {
    switch (type) {
        case TokenType::DEFINE: return "define";
        case TokenType::ARGUMENT: return "argument";
        case TokenType::FORMAT: return "format";
        case TokenType::STRUCT: return "struct";
        case TokenType::PROMPT: return "prompt";
        case TokenType::CHANNEL: return "channel";
        case TokenType::FLOW: return "flow";
        case TokenType::RETURN: return "return";
        case TokenType::ANNOTATE: return "annotate";
        case TokenType::TO: return "to";
        case TokenType::FROM: return "from";
        case TokenType::CALL: return "call";
        case TokenType::EXTERN: return "extern";
        case TokenType::ENTRY: return "entry";
        case TokenType::KWARG: return "kwarg";
        case TokenType::MAP: return "map";
        case TokenType::BIND: return "bind";
        case TokenType::AS: return "as";
        case TokenType::IS: return "is";
        case TokenType::SEARCH: return "search";
        case TokenType::TEXT: return "text";
        case TokenType::SELECT: return "select";
        case TokenType::REPEAT: return "repeat";
        case TokenType::IDENTIFIER: return "identifier";
        case TokenType::STRING_LITERAL: return "string";
        case TokenType::INTEGER_LITERAL: return "integer literal";
        case TokenType::FLOAT_LITERAL: return "float literal";
        case TokenType::LBRACE: return "'{'";
        case TokenType::RBRACE: return "'}'";
        case TokenType::LSQUARE: return "'['";
        case TokenType::RSQUARE: return "']'";
        case TokenType::LPAREN: return "'('";
        case TokenType::RPAREN: return "')'";
        case TokenType::SEMICOLON: return "';'";
        case TokenType::COLON: return "':'";
        case TokenType::COMMA: return "','";
        case TokenType::DOT: return "'.'";
        case TokenType::EQUAL: return "'='";
        case TokenType::PLUS: return "'+'";
        case TokenType::MINUS: return "'-'";
        case TokenType::STAR: return "'*'";
        case TokenType::SLASH: return "'/'";
        case TokenType::ERROR: return "invalid token";
        case TokenType::END_OF_FILE: return "end of file";
        default: return "unknown";
    }
}

}
