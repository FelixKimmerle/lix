#pragma once

#include "Expr.hpp"
#include "TypeExpr.hpp"

#include <memory>
#include <string>
#include <vector>

struct ExprStmt;
struct PrintStmt;
struct VarStmt;
struct BlockStmt;
struct IfStmt;
struct WhileStmt;
struct ForStmt;
struct ReturnStmt;
struct BreakStmt;
struct ContinueStmt;
struct FuncDecl;
struct ClassDecl;

class VisitorStmt
{
  public:
    virtual ~VisitorStmt() = default;
    virtual void visitExprStmt(ExprStmt* stmt) = 0;
    virtual void visitPrint(PrintStmt* stmt) = 0;
    virtual void visitVarStmt(VarStmt* stmt) = 0;
    virtual void visitBlock(BlockStmt* stmt) = 0;
    virtual void visitIf(IfStmt* stmt) = 0;
    virtual void visitWhile(WhileStmt* stmt) = 0;
    virtual void visitFor(ForStmt* stmt) = 0;
    virtual void visitReturn(ReturnStmt* stmt) = 0;
    virtual void visitBreak(BreakStmt* stmt) = 0;
    virtual void visitContinue(ContinueStmt* stmt) = 0;
    virtual void visitFuncDecl(FuncDecl* stmt) = 0;
    virtual void visitClassDecl(ClassDecl* stmt) = 0;
};

struct Stmt
{
    SourceSector sector;

    explicit Stmt(SourceSector sector) : sector(sector)
    {
    }
    virtual ~Stmt() = default;
    virtual void visit(VisitorStmt* visitor) = 0;
};

using StmtPtr = std::unique_ptr<Stmt>;

#define LIX_VISIT_STMT(Name)                                                             \
    void visit(VisitorStmt* visitor) override                                            \
    {                                                                                    \
        visitor->visit##Name(this);                                                      \
    }

struct ExprStmt : Stmt
{
    ExprPtr expr;
    ExprStmt(ExprPtr expr, SourceSector sector) : Stmt(sector), expr(std::move(expr))
    {
    }
    LIX_VISIT_STMT(ExprStmt)
};

struct PrintStmt : Stmt
{
    ExprPtr expr;
    PrintStmt(ExprPtr expr, SourceSector sector) : Stmt(sector), expr(std::move(expr))
    {
    }
    LIX_VISIT_STMT(Print)
};

// 'int x = 5;', 'auto v = [1, 2];' or 'vector<Point> points;'. Also used for fields.
struct VarStmt : Stmt
{
    TypeExprPtr type;
    std::string name;
    ExprPtr initializer; // may be null
    VarStmt(TypeExprPtr type, std::string name, ExprPtr initializer, SourceSector sector)
        : Stmt(sector), type(std::move(type)), name(std::move(name)),
          initializer(std::move(initializer))
    {
    }
    LIX_VISIT_STMT(VarStmt)
};

struct BlockStmt : Stmt
{
    std::vector<StmtPtr> statements;
    explicit BlockStmt(SourceSector sector) : Stmt(sector)
    {
    }
    LIX_VISIT_STMT(Block)
};

struct IfStmt : Stmt
{
    ExprPtr condition;
    StmtPtr then_branch;
    StmtPtr else_branch; // may be null
    IfStmt(ExprPtr condition, StmtPtr then_branch, StmtPtr else_branch,
           SourceSector sector)
        : Stmt(sector), condition(std::move(condition)),
          then_branch(std::move(then_branch)), else_branch(std::move(else_branch))
    {
    }
    LIX_VISIT_STMT(If)
};

struct WhileStmt : Stmt
{
    ExprPtr condition;
    StmtPtr body;
    WhileStmt(ExprPtr condition, StmtPtr body, SourceSector sector)
        : Stmt(sector), condition(std::move(condition)), body(std::move(body))
    {
    }
    LIX_VISIT_STMT(While)
};

struct ForStmt : Stmt
{
    StmtPtr initializer; // may be null
    ExprPtr condition;   // may be null
    ExprPtr increment;   // may be null
    StmtPtr body;
    ForStmt(StmtPtr initializer, ExprPtr condition, ExprPtr increment, StmtPtr body,
            SourceSector sector)
        : Stmt(sector), initializer(std::move(initializer)),
          condition(std::move(condition)), increment(std::move(increment)),
          body(std::move(body))
    {
    }
    LIX_VISIT_STMT(For)
};

struct ReturnStmt : Stmt
{
    ExprPtr value; // may be null
    ReturnStmt(ExprPtr value, SourceSector sector) : Stmt(sector), value(std::move(value))
    {
    }
    LIX_VISIT_STMT(Return)
};

struct BreakStmt : Stmt
{
    explicit BreakStmt(SourceSector sector) : Stmt(sector)
    {
    }
    LIX_VISIT_STMT(Break)
};

struct ContinueStmt : Stmt
{
    explicit ContinueStmt(SourceSector sector) : Stmt(sector)
    {
    }
    LIX_VISIT_STMT(Continue)
};

struct Param
{
    TypeExprPtr type;
    std::string name;
    SourceSector sector;
};

// A function, method or constructor. Functions may be templates:
//
//   template <typename T> T max(T a, T b) { ... }
struct FuncDecl : Stmt
{
    std::vector<std::string> type_params;
    TypeExprPtr return_type; // null for constructors
    std::string name;
    std::vector<Param> params;
    std::unique_ptr<BlockStmt> body;
    SourceSector name_sector;

    FuncDecl(SourceSector sector) : Stmt(sector)
    {
    }
    bool is_constructor() const
    {
        return return_type == nullptr;
    }
    LIX_VISIT_STMT(FuncDecl)
};

// A class, possibly a template:
//
//   template <typename T> class Box { T value; Box(T v) { value = v; } T get() { ... } }
struct ClassDecl : Stmt
{
    std::vector<std::string> type_params;
    std::string name;
    std::vector<std::unique_ptr<VarStmt>> fields;
    std::vector<std::unique_ptr<FuncDecl>> methods;
    std::unique_ptr<FuncDecl> constructor; // may be null
    SourceSector name_sector;

    ClassDecl(SourceSector sector) : Stmt(sector)
    {
    }
    LIX_VISIT_STMT(ClassDecl)
};

#undef LIX_VISIT_STMT
