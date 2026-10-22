#include "Token.hpp"

#include <ostream>

const char* token_type_name(TokenType type)
{
    switch (type)
    {
    case TokenType::LEFT_PAREN:
        return "'('";
    case TokenType::RIGHT_PAREN:
        return "')'";
    case TokenType::LEFT_BRACE:
        return "'{'";
    case TokenType::RIGHT_BRACE:
        return "'}'";
    case TokenType::LEFT_BRACKET:
        return "'['";
    case TokenType::RIGHT_BRACKET:
        return "']'";
    case TokenType::COMMA:
        return "','";
    case TokenType::DOT:
        return "'.'";
    case TokenType::SEMICOLON:
        return "';'";
    case TokenType::COLON:
        return "':'";
    case TokenType::PLUS:
        return "'+'";
    case TokenType::MINUS:
        return "'-'";
    case TokenType::STAR:
        return "'*'";
    case TokenType::SLASH:
        return "'/'";
    case TokenType::PERCENT:
        return "'%'";
    case TokenType::BANG:
        return "'!'";
    case TokenType::BANG_EQUAL:
        return "'!='";
    case TokenType::EQUAL:
        return "'='";
    case TokenType::EQUAL_EQUAL:
        return "'=='";
    case TokenType::GREATER:
        return "'>'";
    case TokenType::GREATER_EQUAL:
        return "'>='";
    case TokenType::LESS:
        return "'<'";
    case TokenType::LESS_EQUAL:
        return "'<='";
    case TokenType::AMP_AMP:
        return "'&&'";
    case TokenType::PIPE_PIPE:
        return "'||'";
    case TokenType::PLUS_EQUAL:
        return "'+='";
    case TokenType::MINUS_EQUAL:
        return "'-='";
    case TokenType::STAR_EQUAL:
        return "'*='";
    case TokenType::SLASH_EQUAL:
        return "'/='";
    case TokenType::PERCENT_EQUAL:
        return "'%='";
    case TokenType::PLUS_PLUS:
        return "'++'";
    case TokenType::MINUS_MINUS:
        return "'--'";
    case TokenType::IDENTIFIER:
        return "identifier";
    case TokenType::STRING:
        return "string literal";
    case TokenType::INT:
        return "integer literal";
    case TokenType::FLOAT:
        return "float literal";
    case TokenType::AND:
        return "'and'";
    case TokenType::OR:
        return "'or'";
    case TokenType::IF:
        return "'if'";
    case TokenType::ELSE:
        return "'else'";
    case TokenType::WHILE:
        return "'while'";
    case TokenType::FOR:
        return "'for'";
    case TokenType::BREAK:
        return "'break'";
    case TokenType::CONTINUE:
        return "'continue'";
    case TokenType::RETURN:
        return "'return'";
    case TokenType::PRINT:
        return "'print'";
    case TokenType::CLASS:
        return "'class'";
    case TokenType::TEMPLATE:
        return "'template'";
    case TokenType::TYPENAME:
        return "'typename'";
    case TokenType::THIS:
        return "'this'";
    case TokenType::TRUE:
        return "'true'";
    case TokenType::FALSE:
        return "'false'";
    case TokenType::NULL_LITERAL:
        return "'null'";
    case TokenType::CAST:
        return "'cast'";
    case TokenType::TYPE_INT:
        return "'int'";
    case TokenType::TYPE_FLOAT:
        return "'float'";
    case TokenType::TYPE_BOOL:
        return "'bool'";
    case TokenType::TYPE_STRING:
        return "'string'";
    case TokenType::TYPE_VOID:
        return "'void'";
    case TokenType::TYPE_AUTO:
        return "'auto'";
    case TokenType::ERROR:
        return "error";
    case TokenType::END_OF_FILE:
        return "end of file";
    }
    return "unknown token";
}

std::ostream& operator<<(std::ostream& out, const Token& token)
{
    return out << token.sector << " " << token_type_name(token.type) << " '"
               << token.lexeme << "'";
}
