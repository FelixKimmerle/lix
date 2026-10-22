#pragma once

#include "Token.hpp"
#include "ast/Expr.hpp"
#include "ast/Stmt.hpp"
#include "common/Diagnostics.hpp"

#include <optional>
#include <vector>

// Recursive descent parser.
//
// Grammar (top-down, lowest precedence first):
//
//   program     -> top_level* EOF
//   top_level   -> template_decl | class_decl | function | statement
//   template    -> "template" "<" "typename" IDENT ("," "typename" IDENT)* ">"
//                  (class_decl | function)
//   class_decl  -> "class" IDENT "{" member* "}" ";"?
//   member      -> IDENT "(" params ")" block                 // constructor
//                | type IDENT "(" params ")" block            // method
//                | type IDENT ("=" expression)? ";"           // field
//   function    -> type IDENT "(" params ")" block
//   statement   -> var_decl | print | if | while | for | return | break | continue
//                | block | expr_stmt
//   var_decl    -> type IDENT ("=" expression)? ";"
//   type        -> (IDENT | int | float | bool | string | void | auto)
//                  ("<" type ("," type)* ">")?
//
//   expression  -> assignment
//   assignment  -> postfix ("=" | "+=" | ...) assignment | logic_or
//   logic_or    -> logic_and (("||" | "or") logic_and)*
//   logic_and   -> equality (("&&" | "and") equality)*
//   equality    -> comparison (("==" | "!=") comparison)*
//   comparison  -> term (("<" | "<=" | ">" | ">=") term)*
//   term        -> factor (("+" | "-") factor)*
//   factor      -> unary (("*" | "/" | "%") unary)*
//   unary       -> ("!" | "-" | "++" | "--") unary | postfix
//   postfix     -> primary ("(" args ")" | "." IDENT | "[" expression "]" | "++" | "--")*
//   primary     -> literal | IDENT type_args? | "this" | "(" expression ")"
//                | "[" args "]" | "cast" "<" type ">" "(" expression ")"
//
// Ambiguities between declarations and expressions ('Box<int> b;' versus 'a < b;') and
// between generic calls and comparisons ('max<int>(a)' versus 'a < b') are resolved by
// speculatively parsing a type and backtracking if that fails.
class Parser
{
  private:
    struct ParseError
    {
    };

    std::vector<Token> tokens;
    size_t pos = 0;
    Diagnostics& diagnostics;
    bool failed = false;
    bool failed_at_end = false;

    const Token& peek(size_t ahead = 0) const;
    const Token& previous() const;
    bool is_at_end() const;
    bool check(TokenType type) const;
    bool match(TokenType type);
    bool match(std::initializer_list<TokenType> types);
    const Token& advance();
    const Token& consume(TokenType type, const std::string& message);
    ParseError error(const Token& token, const std::string& message);
    void synchronize();
    void recover(size_t start);

    // Types
    bool is_type_start(TokenType type) const;
    TypeExprPtr try_type();
    TypeExprPtr type();
    std::optional<std::vector<TypeExprPtr>> try_call_type_args();
    bool looks_like_declaration();

    // Declarations
    StmtPtr top_level();
    StmtPtr template_decl();
    std::unique_ptr<ClassDecl> class_decl(std::vector<std::string> type_params,
                                          SourceSector start);
    std::unique_ptr<FuncDecl> function(std::vector<std::string> type_params,
                                       TypeExprPtr return_type, const Token& name);
    std::unique_ptr<VarStmt> var_declaration(TypeExprPtr type, const Token& name);

    // Statements
    StmtPtr statement();
    std::unique_ptr<BlockStmt> block();
    StmtPtr print_statement();
    StmtPtr if_statement();
    StmtPtr while_statement();
    StmtPtr for_statement();
    StmtPtr return_statement();
    StmtPtr expression_statement();

    // Expressions
    ExprPtr expression();
    ExprPtr assignment();
    ExprPtr logic_or();
    ExprPtr logic_and();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr term();
    ExprPtr factor();
    ExprPtr unary();
    ExprPtr postfix();
    ExprPtr primary();
    std::vector<ExprPtr> arguments(TokenType closing);

  public:
    Parser(std::vector<Token> tokens, Diagnostics& diagnostics);
    std::vector<StmtPtr> parse();

    // After parse(): whether there was a syntax error at the end of the input, i.e. the
    // input is probably just incomplete. The REPL uses this to keep reading lines.
    bool had_error_at_end() const;
    bool had_error() const;
};
