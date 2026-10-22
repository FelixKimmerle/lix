#include "Parser.hpp"

#include <charconv>
#include <cstdlib>

Parser::Parser(std::vector<Token> tokens, Diagnostics& diagnostics)
    : tokens(std::move(tokens)), diagnostics(diagnostics)
{
}

std::vector<StmtPtr> Parser::parse()
{
    std::vector<StmtPtr> statements;
    while (!is_at_end())
    {
        size_t start = pos;
        try
        {
            statements.push_back(top_level());
        }
        catch (const ParseError&)
        {
            recover(start);
        }
    }
    return statements;
}

// ----------------------------------------------------------------------------------
// Token helpers
// ----------------------------------------------------------------------------------

const Token& Parser::peek(size_t ahead) const
{
    size_t index = pos + ahead;
    return index < tokens.size() ? tokens[index] : tokens.back();
}

const Token& Parser::previous() const
{
    return tokens[pos - 1];
}

bool Parser::is_at_end() const
{
    return peek().type == TokenType::END_OF_FILE;
}

bool Parser::check(TokenType type) const
{
    return peek().type == type;
}

bool Parser::match(TokenType type)
{
    if (check(type))
    {
        advance();
        return true;
    }
    return false;
}

bool Parser::match(std::initializer_list<TokenType> types)
{
    for (TokenType type : types)
    {
        if (match(type))
        {
            return true;
        }
    }
    return false;
}

const Token& Parser::advance()
{
    if (!is_at_end())
    {
        pos++;
    }
    return previous();
}

const Token& Parser::consume(TokenType type, const std::string& message)
{
    if (check(type))
    {
        return advance();
    }
    throw error(peek(), message);
}

bool Parser::had_error_at_end() const
{
    return failed_at_end;
}

bool Parser::had_error() const
{
    return failed;
}

Parser::ParseError Parser::error(const Token& token, const std::string& message)
{
    failed = true;
    failed_at_end = failed_at_end || token.type == TokenType::END_OF_FILE;
    std::string where =
        token.type == TokenType::END_OF_FILE ? "end of file" : "'" + token.lexeme + "'";
    diagnostics.error(token.sector, message + " (found " + where + ")");
    return ParseError{};
}

void Parser::recover(size_t start)
{
    // Always make progress, otherwise an unexpected token like a stray '}' would be
    // parsed again and again.
    if (pos == start)
    {
        advance();
    }
    synchronize();
}

void Parser::synchronize()
{
    // Skip tokens until we are probably at the start of a new statement.
    while (!is_at_end())
    {
        if (pos > 0 && (previous().type == TokenType::SEMICOLON ||
                        previous().type == TokenType::RIGHT_BRACE))
        {
            return;
        }
        switch (peek().type)
        {
        case TokenType::CLASS:
        case TokenType::TEMPLATE:
        case TokenType::FOR:
        case TokenType::IF:
        case TokenType::WHILE:
        case TokenType::PRINT:
        case TokenType::RETURN:
            return;
        default:
            break;
        }
        advance();
    }
}

// ----------------------------------------------------------------------------------
// Types
// ----------------------------------------------------------------------------------

bool Parser::is_type_start(TokenType type) const
{
    switch (type)
    {
    case TokenType::IDENTIFIER:
    case TokenType::TYPE_INT:
    case TokenType::TYPE_FLOAT:
    case TokenType::TYPE_BOOL:
    case TokenType::TYPE_STRING:
    case TokenType::TYPE_VOID:
    case TokenType::TYPE_AUTO:
        return true;
    default:
        return false;
    }
}

// Parses a type if there is one. On failure nothing is reported and the position is
// restored, so callers can use this to look ahead.
TypeExprPtr Parser::try_type()
{
    size_t save = pos;
    if (!is_type_start(peek().type))
    {
        return nullptr;
    }
    const Token& name = advance();
    auto result = std::make_unique<TypeExpr>();
    result->name = name.lexeme;
    result->sector = name.sector;

    if (check(TokenType::LESS))
    {
        advance();
        do
        {
            TypeExprPtr arg = try_type();
            if (!arg)
            {
                pos = save;
                return nullptr;
            }
            result->args.push_back(std::move(arg));
        } while (match(TokenType::COMMA));

        if (!match(TokenType::GREATER))
        {
            pos = save;
            return nullptr;
        }
        result->sector |= previous().sector;
    }
    return result;
}

TypeExprPtr Parser::type()
{
    TypeExprPtr result = try_type();
    if (!result)
    {
        throw error(peek(), "Expected a type.");
    }
    return result;
}

// After an identifier: '<' type, ... '>' followed by '(' makes this a generic call or
// a construction like 'Box<int>(5)'. Anything else is left alone, so 'a < b' still
// parses as a comparison.
std::optional<std::vector<TypeExprPtr>> Parser::try_call_type_args()
{
    size_t save = pos;
    if (!match(TokenType::LESS))
    {
        return std::nullopt;
    }
    std::vector<TypeExprPtr> args;
    do
    {
        TypeExprPtr arg = try_type();
        if (!arg)
        {
            pos = save;
            return std::nullopt;
        }
        args.push_back(std::move(arg));
    } while (match(TokenType::COMMA));

    if (!match(TokenType::GREATER) || !check(TokenType::LEFT_PAREN))
    {
        pos = save;
        return std::nullopt;
    }
    return args;
}

// A statement is a declaration if it starts with a type followed by a name:
// 'int x', 'Point p', 'vector<vector<int>> grid', 'auto y'. An expression can never
// look like that, because two names never follow each other in an expression.
bool Parser::looks_like_declaration()
{
    if (is_type_start(peek().type) && peek().type != TokenType::IDENTIFIER)
    {
        return true;
    }
    size_t save = pos;
    bool result = try_type() != nullptr && check(TokenType::IDENTIFIER);
    pos = save;
    return result;
}

// ----------------------------------------------------------------------------------
// Declarations
// ----------------------------------------------------------------------------------

StmtPtr Parser::top_level()
{
    if (check(TokenType::TEMPLATE))
    {
        return template_decl();
    }
    if (check(TokenType::CLASS))
    {
        SourceSector start = advance().sector;
        return class_decl({}, start);
    }
    if (looks_like_declaration())
    {
        TypeExprPtr decl_type = type();
        const Token& name = consume(TokenType::IDENTIFIER, "Expected a name.");
        if (check(TokenType::LEFT_PAREN))
        {
            return function({}, std::move(decl_type), name);
        }
        return var_declaration(std::move(decl_type), name);
    }
    return statement();
}

StmtPtr Parser::template_decl()
{
    SourceSector start = advance().sector;
    consume(TokenType::LESS, "Expected '<' after 'template'.");
    std::vector<std::string> type_params;
    do
    {
        consume(TokenType::TYPENAME, "Expected 'typename'.");
        type_params.push_back(
            consume(TokenType::IDENTIFIER, "Expected a type parameter name.").lexeme);
    } while (match(TokenType::COMMA));
    consume(TokenType::GREATER, "Expected '>' after template parameters.");

    if (match(TokenType::CLASS))
    {
        return class_decl(std::move(type_params), start);
    }
    TypeExprPtr return_type = type();
    const Token& name = consume(TokenType::IDENTIFIER, "Expected a function name.");
    if (!check(TokenType::LEFT_PAREN))
    {
        throw error(peek(), "Expected '(' after template function name.");
    }
    auto func = function(std::move(type_params), std::move(return_type), name);
    func->sector |= start;
    return func;
}

std::unique_ptr<ClassDecl> Parser::class_decl(std::vector<std::string> type_params,
                                              SourceSector start)
{
    const Token& name = consume(TokenType::IDENTIFIER, "Expected a class name.");
    auto decl = std::make_unique<ClassDecl>(start | name.sector);
    decl->type_params = std::move(type_params);
    decl->name = name.lexeme;
    decl->name_sector = name.sector;

    consume(TokenType::LEFT_BRACE, "Expected '{' before class body.");
    while (!check(TokenType::RIGHT_BRACE) && !is_at_end())
    {
        if (check(TokenType::TEMPLATE))
        {
            throw error(peek(), "Template methods are not supported.");
        }
        // Constructor: the class name directly followed by '('.
        if (check(TokenType::IDENTIFIER) && peek().lexeme == decl->name &&
            peek(1).type == TokenType::LEFT_PAREN)
        {
            const Token& ctor_name = advance();
            if (decl->constructor)
            {
                throw error(ctor_name, "A class can only have one constructor.");
            }
            decl->constructor = function({}, nullptr, ctor_name);
            continue;
        }

        TypeExprPtr member_type = type();
        const Token& member_name =
            consume(TokenType::IDENTIFIER, "Expected a member name.");
        if (check(TokenType::LEFT_PAREN))
        {
            decl->methods.push_back(function({}, std::move(member_type), member_name));
        }
        else
        {
            decl->fields.push_back(var_declaration(std::move(member_type), member_name));
        }
    }
    consume(TokenType::RIGHT_BRACE, "Expected '}' after class body.");
    decl->sector |= previous().sector;
    match(TokenType::SEMICOLON); // C++ style trailing ';' is optional
    return decl;
}

std::unique_ptr<FuncDecl> Parser::function(std::vector<std::string> type_params,
                                           TypeExprPtr return_type, const Token& name)
{
    auto func = std::make_unique<FuncDecl>(return_type ? return_type->sector | name.sector
                                                       : name.sector);
    func->type_params = std::move(type_params);
    func->return_type = std::move(return_type);
    func->name = name.lexeme;
    func->name_sector = name.sector;

    consume(TokenType::LEFT_PAREN, "Expected '(' after function name.");
    if (!check(TokenType::RIGHT_PAREN))
    {
        do
        {
            Param param;
            param.type = type();
            const Token& param_name =
                consume(TokenType::IDENTIFIER, "Expected a parameter name.");
            param.name = param_name.lexeme;
            param.sector = param.type->sector | param_name.sector;
            func->params.push_back(std::move(param));
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expected ')' after parameters.");
    consume(TokenType::LEFT_BRACE, "Expected '{' before function body.");
    func->body = block();
    return func;
}

std::unique_ptr<VarStmt> Parser::var_declaration(TypeExprPtr var_type, const Token& name)
{
    ExprPtr initializer = nullptr;
    if (match(TokenType::EQUAL))
    {
        initializer = expression();
    }
    consume(TokenType::SEMICOLON, "Expected ';' after variable declaration.");
    return std::make_unique<VarStmt>(std::move(var_type), name.lexeme,
                                     std::move(initializer), name.sector);
}

// ----------------------------------------------------------------------------------
// Statements
// ----------------------------------------------------------------------------------

StmtPtr Parser::statement()
{
    if (check(TokenType::CLASS) || check(TokenType::TEMPLATE))
    {
        throw error(peek(),
                    "Classes and templates can only be declared at the top level.");
    }
    if (match(TokenType::PRINT))
    {
        return print_statement();
    }
    if (match(TokenType::IF))
    {
        return if_statement();
    }
    if (match(TokenType::WHILE))
    {
        return while_statement();
    }
    if (match(TokenType::FOR))
    {
        return for_statement();
    }
    if (match(TokenType::RETURN))
    {
        return return_statement();
    }
    if (match(TokenType::BREAK))
    {
        SourceSector sector = previous().sector;
        consume(TokenType::SEMICOLON, "Expected ';' after 'break'.");
        return std::make_unique<BreakStmt>(sector);
    }
    if (match(TokenType::CONTINUE))
    {
        SourceSector sector = previous().sector;
        consume(TokenType::SEMICOLON, "Expected ';' after 'continue'.");
        return std::make_unique<ContinueStmt>(sector);
    }
    if (match(TokenType::LEFT_BRACE))
    {
        return block();
    }
    if (looks_like_declaration())
    {
        TypeExprPtr decl_type = type();
        const Token& name = consume(TokenType::IDENTIFIER, "Expected a name.");
        if (check(TokenType::LEFT_PAREN))
        {
            throw error(name, "Functions can only be declared at the top level.");
        }
        return var_declaration(std::move(decl_type), name);
    }
    return expression_statement();
}

// Called after the opening '{' has been consumed.
std::unique_ptr<BlockStmt> Parser::block()
{
    auto result = std::make_unique<BlockStmt>(previous().sector);
    while (!check(TokenType::RIGHT_BRACE) && !is_at_end())
    {
        size_t start = pos;
        try
        {
            result->statements.push_back(statement());
        }
        catch (const ParseError&)
        {
            recover(start);
        }
    }
    consume(TokenType::RIGHT_BRACE, "Expected '}' after block.");
    result->sector |= previous().sector;
    return result;
}

StmtPtr Parser::print_statement()
{
    SourceSector sector = previous().sector;
    ExprPtr value = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after value.");
    return std::make_unique<PrintStmt>(std::move(value), sector | previous().sector);
}

StmtPtr Parser::if_statement()
{
    SourceSector sector = previous().sector;
    consume(TokenType::LEFT_PAREN, "Expected '(' after 'if'.");
    ExprPtr condition = expression();
    consume(TokenType::RIGHT_PAREN, "Expected ')' after condition.");
    StmtPtr then_branch = statement();
    StmtPtr else_branch = nullptr;
    if (match(TokenType::ELSE))
    {
        else_branch = statement();
    }
    return std::make_unique<IfStmt>(std::move(condition), std::move(then_branch),
                                    std::move(else_branch), sector);
}

StmtPtr Parser::while_statement()
{
    SourceSector sector = previous().sector;
    consume(TokenType::LEFT_PAREN, "Expected '(' after 'while'.");
    ExprPtr condition = expression();
    consume(TokenType::RIGHT_PAREN, "Expected ')' after condition.");
    StmtPtr body = statement();
    return std::make_unique<WhileStmt>(std::move(condition), std::move(body), sector);
}

StmtPtr Parser::for_statement()
{
    SourceSector sector = previous().sector;
    consume(TokenType::LEFT_PAREN, "Expected '(' after 'for'.");

    StmtPtr initializer = nullptr;
    if (match(TokenType::SEMICOLON))
    {
    }
    else if (looks_like_declaration())
    {
        TypeExprPtr decl_type = type();
        const Token& name = consume(TokenType::IDENTIFIER, "Expected a name.");
        initializer = var_declaration(std::move(decl_type), name);
    }
    else
    {
        initializer = expression_statement();
    }

    ExprPtr condition = nullptr;
    if (!check(TokenType::SEMICOLON))
    {
        condition = expression();
    }
    consume(TokenType::SEMICOLON, "Expected ';' after loop condition.");

    ExprPtr increment = nullptr;
    if (!check(TokenType::RIGHT_PAREN))
    {
        increment = expression();
    }
    consume(TokenType::RIGHT_PAREN, "Expected ')' after for clauses.");

    StmtPtr body = statement();
    return std::make_unique<ForStmt>(std::move(initializer), std::move(condition),
                                     std::move(increment), std::move(body), sector);
}

StmtPtr Parser::return_statement()
{
    SourceSector sector = previous().sector;
    ExprPtr value = nullptr;
    if (!check(TokenType::SEMICOLON))
    {
        value = expression();
    }
    consume(TokenType::SEMICOLON, "Expected ';' after return value.");
    return std::make_unique<ReturnStmt>(std::move(value), sector | previous().sector);
}

StmtPtr Parser::expression_statement()
{
    ExprPtr expr = expression();
    SourceSector sector = expr->sector;
    consume(TokenType::SEMICOLON, "Expected ';' after expression.");
    return std::make_unique<ExprStmt>(std::move(expr), sector);
}

// ----------------------------------------------------------------------------------
// Expressions
// ----------------------------------------------------------------------------------

ExprPtr Parser::expression()
{
    return assignment();
}

ExprPtr Parser::assignment()
{
    ExprPtr expr = logic_or();

    std::optional<BinaryExpr::Op> op;
    switch (peek().type)
    {
    case TokenType::EQUAL:
        break;
    case TokenType::PLUS_EQUAL:
        op = BinaryExpr::Op::ADD;
        break;
    case TokenType::MINUS_EQUAL:
        op = BinaryExpr::Op::SUB;
        break;
    case TokenType::STAR_EQUAL:
        op = BinaryExpr::Op::MUL;
        break;
    case TokenType::SLASH_EQUAL:
        op = BinaryExpr::Op::DIV;
        break;
    case TokenType::PERCENT_EQUAL:
        op = BinaryExpr::Op::MOD;
        break;
    default:
        return expr;
    }
    const Token& equals = advance();
    ExprPtr value = assignment(); // right associative: a = b = c

    if (!dynamic_cast<VarExpr*>(expr.get()) && !dynamic_cast<GetExpr*>(expr.get()) &&
        !dynamic_cast<IndexExpr*>(expr.get()))
    {
        error(equals, "Invalid assignment target.");
        return expr;
    }
    SourceSector sector = expr->sector | value->sector;
    return std::make_unique<AssignExpr>(std::move(expr), op, std::move(value), sector);
}

ExprPtr Parser::logic_or()
{
    ExprPtr expr = logic_and();
    while (match({TokenType::PIPE_PIPE, TokenType::OR}))
    {
        ExprPtr right = logic_and();
        SourceSector sector = expr->sector | right->sector;
        expr = std::make_unique<BinaryExpr>(BinaryExpr::Op::OR, std::move(expr),
                                            std::move(right), sector);
    }
    return expr;
}

ExprPtr Parser::logic_and()
{
    ExprPtr expr = equality();
    while (match({TokenType::AMP_AMP, TokenType::AND}))
    {
        ExprPtr right = equality();
        SourceSector sector = expr->sector | right->sector;
        expr = std::make_unique<BinaryExpr>(BinaryExpr::Op::AND, std::move(expr),
                                            std::move(right), sector);
    }
    return expr;
}

ExprPtr Parser::equality()
{
    ExprPtr expr = comparison();
    while (match({TokenType::EQUAL_EQUAL, TokenType::BANG_EQUAL}))
    {
        BinaryExpr::Op op = previous().type == TokenType::EQUAL_EQUAL
                                ? BinaryExpr::Op::EQUAL
                                : BinaryExpr::Op::NOT_EQUAL;
        ExprPtr right = comparison();
        SourceSector sector = expr->sector | right->sector;
        expr =
            std::make_unique<BinaryExpr>(op, std::move(expr), std::move(right), sector);
    }
    return expr;
}

ExprPtr Parser::comparison()
{
    ExprPtr expr = term();
    while (match({TokenType::LESS, TokenType::LESS_EQUAL, TokenType::GREATER,
                  TokenType::GREATER_EQUAL}))
    {
        BinaryExpr::Op op;
        switch (previous().type)
        {
        case TokenType::LESS:
            op = BinaryExpr::Op::LESS;
            break;
        case TokenType::LESS_EQUAL:
            op = BinaryExpr::Op::LESS_EQUAL;
            break;
        case TokenType::GREATER:
            op = BinaryExpr::Op::GREATER;
            break;
        default:
            op = BinaryExpr::Op::GREATER_EQUAL;
            break;
        }
        ExprPtr right = term();
        SourceSector sector = expr->sector | right->sector;
        expr =
            std::make_unique<BinaryExpr>(op, std::move(expr), std::move(right), sector);
    }
    return expr;
}

ExprPtr Parser::term()
{
    ExprPtr expr = factor();
    while (match({TokenType::PLUS, TokenType::MINUS}))
    {
        BinaryExpr::Op op = previous().type == TokenType::PLUS ? BinaryExpr::Op::ADD
                                                               : BinaryExpr::Op::SUB;
        ExprPtr right = factor();
        SourceSector sector = expr->sector | right->sector;
        expr =
            std::make_unique<BinaryExpr>(op, std::move(expr), std::move(right), sector);
    }
    return expr;
}

ExprPtr Parser::factor()
{
    ExprPtr expr = unary();
    while (match({TokenType::STAR, TokenType::SLASH, TokenType::PERCENT}))
    {
        BinaryExpr::Op op;
        switch (previous().type)
        {
        case TokenType::STAR:
            op = BinaryExpr::Op::MUL;
            break;
        case TokenType::SLASH:
            op = BinaryExpr::Op::DIV;
            break;
        default:
            op = BinaryExpr::Op::MOD;
            break;
        }
        ExprPtr right = unary();
        SourceSector sector = expr->sector | right->sector;
        expr =
            std::make_unique<BinaryExpr>(op, std::move(expr), std::move(right), sector);
    }
    return expr;
}

ExprPtr Parser::unary()
{
    if (match({TokenType::BANG, TokenType::MINUS}))
    {
        const Token op = previous();
        ExprPtr operand = unary();
        SourceSector sector = op.sector | operand->sector;
        return std::make_unique<UnaryExpr>(
            op.type == TokenType::BANG ? UnaryExpr::Op::NOT : UnaryExpr::Op::NEGATE,
            std::move(operand), sector);
    }
    if (match({TokenType::PLUS_PLUS, TokenType::MINUS_MINUS}))
    {
        const Token op = previous();
        ExprPtr operand = unary();
        SourceSector sector = op.sector | operand->sector;
        return std::make_unique<IncrementExpr>(
            std::move(operand), op.type == TokenType::PLUS_PLUS ? 1 : -1, true, sector);
    }
    return postfix();
}

std::vector<ExprPtr> Parser::arguments(TokenType closing)
{
    std::vector<ExprPtr> args;
    if (!check(closing))
    {
        do
        {
            args.push_back(expression());
        } while (match(TokenType::COMMA));
    }
    consume(closing,
            std::string("Expected ") + token_type_name(closing) + " after arguments.");
    return args;
}

ExprPtr Parser::postfix()
{
    ExprPtr expr = primary();
    for (;;)
    {
        if (match(TokenType::LEFT_PAREN))
        {
            std::vector<ExprPtr> args = arguments(TokenType::RIGHT_PAREN);
            SourceSector sector = expr->sector | previous().sector;
            expr = std::make_unique<CallExpr>(std::move(expr), std::vector<TypeExprPtr>{},
                                              std::move(args), sector);
        }
        else if (match(TokenType::DOT))
        {
            const Token& name =
                consume(TokenType::IDENTIFIER, "Expected a member name after '.'.");
            SourceSector sector = expr->sector | name.sector;
            expr = std::make_unique<GetExpr>(std::move(expr), name.lexeme, sector);
        }
        else if (match(TokenType::LEFT_BRACKET))
        {
            ExprPtr index = expression();
            consume(TokenType::RIGHT_BRACKET, "Expected ']' after index.");
            SourceSector sector = expr->sector | previous().sector;
            expr = std::make_unique<IndexExpr>(std::move(expr), std::move(index), sector);
        }
        else if (match({TokenType::PLUS_PLUS, TokenType::MINUS_MINUS}))
        {
            int delta = previous().type == TokenType::PLUS_PLUS ? 1 : -1;
            SourceSector sector = expr->sector | previous().sector;
            expr = std::make_unique<IncrementExpr>(std::move(expr), delta, false, sector);
        }
        else
        {
            return expr;
        }
    }
}

ExprPtr Parser::primary()
{
    const Token& token = peek();
    switch (token.type)
    {
    case TokenType::TRUE:
        advance();
        return std::make_unique<BoolExpr>(true, token.sector);
    case TokenType::FALSE:
        advance();
        return std::make_unique<BoolExpr>(false, token.sector);
    case TokenType::NULL_LITERAL:
        advance();
        return std::make_unique<NullExpr>(token.sector);
    case TokenType::THIS:
        advance();
        return std::make_unique<ThisExpr>(token.sector);
    case TokenType::INT: {
        advance();
        int64_t value = 0;
        const char* begin = token.lexeme.data();
        auto [end, err] = std::from_chars(begin, begin + token.lexeme.size(), value);
        if (err != std::errc() || end != begin + token.lexeme.size())
        {
            throw error(token, "Integer literal is too large.");
        }
        return std::make_unique<IntExpr>(value, token.sector);
    }
    case TokenType::FLOAT:
        advance();
        return std::make_unique<FloatExpr>(std::strtod(token.lexeme.c_str(), nullptr),
                                           token.sector);
    case TokenType::STRING:
        advance();
        return std::make_unique<StringExpr>(token.lexeme, token.sector);
    case TokenType::IDENTIFIER: {
        advance();
        auto var = std::make_unique<VarExpr>(token.lexeme, token.sector);
        // 'max<int>(...)', 'Box<int>(...)', 'vector<int>()'
        if (auto type_args = try_call_type_args())
        {
            consume(TokenType::LEFT_PAREN, "Expected '('.");
            std::vector<ExprPtr> args = arguments(TokenType::RIGHT_PAREN);
            SourceSector sector = token.sector | previous().sector;
            return std::make_unique<CallExpr>(std::move(var), std::move(*type_args),
                                              std::move(args), sector);
        }
        return var;
    }
    case TokenType::LEFT_PAREN: {
        advance();
        ExprPtr expr = expression();
        consume(TokenType::RIGHT_PAREN, "Expected ')' after expression.");
        return expr;
    }
    case TokenType::LEFT_BRACKET: {
        advance();
        std::vector<ExprPtr> elements = arguments(TokenType::RIGHT_BRACKET);
        return std::make_unique<VectorExpr>(std::move(elements),
                                            token.sector | previous().sector);
    }
    case TokenType::CAST: {
        advance();
        consume(TokenType::LESS, "Expected '<' after 'cast'.");
        TypeExprPtr target = type();
        consume(TokenType::GREATER, "Expected '>' after cast type.");
        consume(TokenType::LEFT_PAREN, "Expected '(' after cast type.");
        ExprPtr expr = expression();
        consume(TokenType::RIGHT_PAREN, "Expected ')' after cast expression.");
        return std::make_unique<CastExpr>(std::move(target), std::move(expr),
                                          token.sector | previous().sector);
    }
    case TokenType::TYPE_INT:
    case TokenType::TYPE_FLOAT:
    case TokenType::TYPE_BOOL:
    case TokenType::TYPE_STRING:
        throw error(token,
                    "Expected an expression. Use cast<T>(value) to convert values");
    default:
        throw error(token, "Expected an expression.");
    }
}
