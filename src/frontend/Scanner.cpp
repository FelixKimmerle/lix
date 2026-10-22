#include "Scanner.hpp"

#include <unordered_map>

namespace
{
    bool is_digit(char c)
    {
        return c >= '0' && c <= '9';
    }

    bool is_alpha(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    const std::unordered_map<std::string, TokenType> keywords = {
        {"and", TokenType::AND},
        {"or", TokenType::OR},
        {"if", TokenType::IF},
        {"else", TokenType::ELSE},
        {"while", TokenType::WHILE},
        {"for", TokenType::FOR},
        {"break", TokenType::BREAK},
        {"continue", TokenType::CONTINUE},
        {"return", TokenType::RETURN},
        {"print", TokenType::PRINT},
        {"class", TokenType::CLASS},
        {"template", TokenType::TEMPLATE},
        {"typename", TokenType::TYPENAME},
        {"this", TokenType::THIS},
        {"true", TokenType::TRUE},
        {"false", TokenType::FALSE},
        {"null", TokenType::NULL_LITERAL},
        {"cast", TokenType::CAST},
        {"int", TokenType::TYPE_INT},
        {"float", TokenType::TYPE_FLOAT},
        {"bool", TokenType::TYPE_BOOL},
        {"string", TokenType::TYPE_STRING},
        {"void", TokenType::TYPE_VOID},
        {"auto", TokenType::TYPE_AUTO},
    };
} // namespace

Scanner::Scanner(const std::string& src, Diagnostics& diagnostics, uint32_t first_line)
    : src(src), diagnostics(diagnostics)
{
    pos_current.line = first_line;
}

std::vector<Token> Scanner::scan_tokens()
{
    std::vector<Token> tokens;
    for (;;)
    {
        Token token = scan_token();
        if (token.type == TokenType::ERROR)
        {
            diagnostics.error(token.sector, token.lexeme);
            continue;
        }
        tokens.push_back(token);
        if (token.type == TokenType::END_OF_FILE)
        {
            return tokens;
        }
    }
}

bool Scanner::is_at_end() const
{
    return current >= src.size();
}

char Scanner::advance()
{
    char c = src[current++];
    if (c == '\n')
    {
        pos_current.line++;
        pos_current.column = 0;
    }
    else
    {
        pos_current.column++;
    }
    return c;
}

bool Scanner::match(char expected)
{
    if (is_at_end() || src[current] != expected)
    {
        return false;
    }
    advance();
    return true;
}

char Scanner::peek() const
{
    return is_at_end() ? '\0' : src[current];
}

char Scanner::peek_next() const
{
    return current + 1 >= src.size() ? '\0' : src[current + 1];
}

void Scanner::skip_whitespace()
{
    for (;;)
    {
        switch (peek())
        {
        case ' ':
        case '\r':
        case '\t':
        case '\n':
            advance();
            break;
        case '/':
            if (peek_next() == '/')
            {
                while (peek() != '\n' && !is_at_end())
                {
                    advance();
                }
            }
            else if (peek_next() == '*')
            {
                advance();
                advance();
                while (!is_at_end() && !(peek() == '*' && peek_next() == '/'))
                {
                    advance();
                }
                if (!is_at_end())
                {
                    advance();
                    advance();
                }
            }
            else
            {
                return;
            }
            break;
        default:
            return;
        }
    }
}

Token Scanner::make_token(TokenType type)
{
    return make_token(type, src.substr(start, current - start));
}

Token Scanner::make_token(TokenType type, std::string lexeme)
{
    return Token{type, std::move(lexeme), SourceSector(pos_start, pos_current)};
}

Token Scanner::error_token(const std::string& message)
{
    return Token{TokenType::ERROR, message, SourceSector(pos_start, pos_current)};
}

Token Scanner::string()
{
    // The lexeme of a string token is its *value*, with escape sequences resolved.
    std::string value;
    while (peek() != '"' && !is_at_end())
    {
        char c = advance();
        if (c == '\\')
        {
            if (is_at_end())
            {
                break;
            }
            char escaped = advance();
            switch (escaped)
            {
            case 'n':
                value += '\n';
                break;
            case 't':
                value += '\t';
                break;
            case 'r':
                value += '\r';
                break;
            case '0':
                value += '\0';
                break;
            case '\\':
                value += '\\';
                break;
            case '"':
                value += '"';
                break;
            default:
                return error_token(std::string("Unknown escape sequence '\\") + escaped +
                                   "'.");
            }
        }
        else
        {
            value += c;
        }
    }

    if (is_at_end())
    {
        return error_token("Unterminated string.");
    }
    advance(); // closing quote
    return make_token(TokenType::STRING, value);
}

Token Scanner::number()
{
    while (is_digit(peek()))
    {
        advance();
    }

    bool is_float = false;
    if (peek() == '.' && is_digit(peek_next()))
    {
        is_float = true;
        advance();
        while (is_digit(peek()))
        {
            advance();
        }
    }
    if (peek() == 'e' || peek() == 'E')
    {
        size_t save = current;
        SourcePosition save_pos = pos_current;
        advance();
        if (peek() == '+' || peek() == '-')
        {
            advance();
        }
        if (is_digit(peek()))
        {
            is_float = true;
            while (is_digit(peek()))
            {
                advance();
            }
        }
        else
        {
            current = save;
            pos_current = save_pos;
        }
    }
    return make_token(is_float ? TokenType::FLOAT : TokenType::INT);
}

Token Scanner::identifier()
{
    while (is_alpha(peek()) || is_digit(peek()))
    {
        advance();
    }
    auto keyword = keywords.find(src.substr(start, current - start));
    if (keyword != keywords.end())
    {
        return make_token(keyword->second);
    }
    return make_token(TokenType::IDENTIFIER);
}

Token Scanner::scan_token()
{
    skip_whitespace();
    start = current;
    pos_start = pos_current;

    if (is_at_end())
    {
        return make_token(TokenType::END_OF_FILE, "");
    }

    char c = advance();
    if (is_alpha(c))
    {
        return identifier();
    }
    if (is_digit(c))
    {
        return number();
    }

    switch (c)
    {
    case '(':
        return make_token(TokenType::LEFT_PAREN);
    case ')':
        return make_token(TokenType::RIGHT_PAREN);
    case '{':
        return make_token(TokenType::LEFT_BRACE);
    case '}':
        return make_token(TokenType::RIGHT_BRACE);
    case '[':
        return make_token(TokenType::LEFT_BRACKET);
    case ']':
        return make_token(TokenType::RIGHT_BRACKET);
    case ',':
        return make_token(TokenType::COMMA);
    case '.':
        return make_token(TokenType::DOT);
    case ';':
        return make_token(TokenType::SEMICOLON);
    case ':':
        return make_token(TokenType::COLON);
    case '+':
        if (match('+'))
        {
            return make_token(TokenType::PLUS_PLUS);
        }
        return make_token(match('=') ? TokenType::PLUS_EQUAL : TokenType::PLUS);
    case '-':
        if (match('-'))
        {
            return make_token(TokenType::MINUS_MINUS);
        }
        return make_token(match('=') ? TokenType::MINUS_EQUAL : TokenType::MINUS);
    case '*':
        return make_token(match('=') ? TokenType::STAR_EQUAL : TokenType::STAR);
    case '/':
        return make_token(match('=') ? TokenType::SLASH_EQUAL : TokenType::SLASH);
    case '%':
        return make_token(match('=') ? TokenType::PERCENT_EQUAL : TokenType::PERCENT);
    case '!':
        return make_token(match('=') ? TokenType::BANG_EQUAL : TokenType::BANG);
    case '=':
        return make_token(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL);
    // There is deliberately no '>>' token, so 'vector<vector<int>>' needs no special
    // handling.
    case '<':
        return make_token(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS);
    case '>':
        return make_token(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);
    case '&':
        if (match('&'))
        {
            return make_token(TokenType::AMP_AMP);
        }
        break;
    case '|':
        if (match('|'))
        {
            return make_token(TokenType::PIPE_PIPE);
        }
        break;
    case '"':
        return string();
    }

    return error_token(std::string("Unexpected character '") + c + "'.");
}
