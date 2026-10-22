#pragma once

#include "Token.hpp"
#include "common/Diagnostics.hpp"

#include <string>
#include <vector>

// Hand written scanner. Turns the source text into a list of tokens. The whole file is
// tokenized up front so the parser can look ahead and backtrack, which it needs to
// tell 'a < b' apart from 'Box<int>'.
class Scanner
{
  private:
    const std::string& src;
    Diagnostics& diagnostics;

    size_t start = 0;
    size_t current = 0;
    SourcePosition pos_start;
    SourcePosition pos_current;

    bool is_at_end() const;
    char advance();
    bool match(char expected);
    char peek() const;
    char peek_next() const;
    void skip_whitespace();

    Token make_token(TokenType type);
    Token make_token(TokenType type, std::string lexeme);
    Token error_token(const std::string& message);
    Token string();
    Token number();
    Token identifier();

    Token scan_token();

  public:
    // first_line: line number of the first line of src (used by the REPL).
    Scanner(const std::string& src, Diagnostics& diagnostics, uint32_t first_line = 0);
    std::vector<Token> scan_tokens();
};
