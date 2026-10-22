#pragma once

#include "common/SourceSector.hpp"

#include <string>

enum class TokenType
{
    // Single-character tokens.
    LEFT_PAREN,
    RIGHT_PAREN,
    LEFT_BRACE,
    RIGHT_BRACE,
    LEFT_BRACKET,
    RIGHT_BRACKET,
    COMMA,
    DOT,
    SEMICOLON,
    COLON,
    PLUS,
    MINUS,
    STAR,
    SLASH,
    PERCENT,

    // One or two character tokens.
    BANG,
    BANG_EQUAL,
    EQUAL,
    EQUAL_EQUAL,
    GREATER,
    GREATER_EQUAL,
    LESS,
    LESS_EQUAL,
    AMP_AMP,
    PIPE_PIPE,
    PLUS_EQUAL,
    MINUS_EQUAL,
    STAR_EQUAL,
    SLASH_EQUAL,
    PERCENT_EQUAL,
    PLUS_PLUS,
    MINUS_MINUS,

    // Literals.
    IDENTIFIER,
    STRING,
    INT,
    FLOAT,

    // Keywords.
    AND,
    OR,
    IF,
    ELSE,
    WHILE,
    FOR,
    BREAK,
    CONTINUE,
    RETURN,
    PRINT,
    CLASS,
    TEMPLATE,
    TYPENAME,
    THIS,
    TRUE,
    FALSE,
    NULL_LITERAL,
    CAST,

    // Built-in type keywords. 'vector' is deliberately *not* a keyword: it is an
    // ordinary identifier that the compiler resolves to the built-in generic type.
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_VOID,
    TYPE_AUTO,

    ERROR,
    END_OF_FILE,
};

struct Token
{
    TokenType type = TokenType::ERROR;
    std::string lexeme;
    SourceSector sector;
};

const char* token_type_name(TokenType type);
std::ostream& operator<<(std::ostream& out, const Token& token);
