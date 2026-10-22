#pragma once

#include "TypeExpr.hpp"
#include "common/SourceSector.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct IntExpr;
struct FloatExpr;
struct BoolExpr;
struct StringExpr;
struct NullExpr;
struct ThisExpr;
struct VarExpr;
struct UnaryExpr;
struct BinaryExpr;
struct AssignExpr;
struct IncrementExpr;
struct CallExpr;
struct GetExpr;
struct IndexExpr;
struct CastExpr;
struct VectorExpr;

class VisitorExpr
{
  public:
    virtual ~VisitorExpr() = default;
    virtual void visitInt(IntExpr* expr) = 0;
    virtual void visitFloat(FloatExpr* expr) = 0;
    virtual void visitBool(BoolExpr* expr) = 0;
    virtual void visitString(StringExpr* expr) = 0;
    virtual void visitNull(NullExpr* expr) = 0;
    virtual void visitThis(ThisExpr* expr) = 0;
    virtual void visitVar(VarExpr* expr) = 0;
    virtual void visitUnary(UnaryExpr* expr) = 0;
    virtual void visitBinary(BinaryExpr* expr) = 0;
    virtual void visitAssign(AssignExpr* expr) = 0;
    virtual void visitIncrement(IncrementExpr* expr) = 0;
    virtual void visitCall(CallExpr* expr) = 0;
    virtual void visitGet(GetExpr* expr) = 0;
    virtual void visitIndex(IndexExpr* expr) = 0;
    virtual void visitCast(CastExpr* expr) = 0;
    virtual void visitVector(VectorExpr* expr) = 0;
};

struct Expr
{
    SourceSector sector;

    explicit Expr(SourceSector sector) : sector(sector)
    {
    }
    virtual ~Expr() = default;
    virtual void visit(VisitorExpr* visitor) = 0;
};

using ExprPtr = std::unique_ptr<Expr>;

#define LIX_VISIT_EXPR(Name)                                                             \
    void visit(VisitorExpr* visitor) override                                            \
    {                                                                                    \
        visitor->visit##Name(this);                                                      \
    }

struct IntExpr : Expr
{
    int64_t value;
    IntExpr(int64_t value, SourceSector sector) : Expr(sector), value(value)
    {
    }
    LIX_VISIT_EXPR(Int)
};

struct FloatExpr : Expr
{
    double value;
    FloatExpr(double value, SourceSector sector) : Expr(sector), value(value)
    {
    }
    LIX_VISIT_EXPR(Float)
};

struct BoolExpr : Expr
{
    bool value;
    BoolExpr(bool value, SourceSector sector) : Expr(sector), value(value)
    {
    }
    LIX_VISIT_EXPR(Bool)
};

struct StringExpr : Expr
{
    std::string value;
    StringExpr(std::string value, SourceSector sector)
        : Expr(sector), value(std::move(value))
    {
    }
    LIX_VISIT_EXPR(String)
};

struct NullExpr : Expr
{
    explicit NullExpr(SourceSector sector) : Expr(sector)
    {
    }
    LIX_VISIT_EXPR(Null)
};

struct ThisExpr : Expr
{
    explicit ThisExpr(SourceSector sector) : Expr(sector)
    {
    }
    LIX_VISIT_EXPR(This)
};

struct VarExpr : Expr
{
    std::string name;
    VarExpr(std::string name, SourceSector sector) : Expr(sector), name(std::move(name))
    {
    }
    LIX_VISIT_EXPR(Var)
};

struct UnaryExpr : Expr
{
    enum class Op
    {
        NEGATE,
        NOT,
    };
    Op op;
    ExprPtr operand;
    UnaryExpr(Op op, ExprPtr operand, SourceSector sector)
        : Expr(sector), op(op), operand(std::move(operand))
    {
    }
    LIX_VISIT_EXPR(Unary)
};

struct BinaryExpr : Expr
{
    enum class Op
    {
        ADD,
        SUB,
        MUL,
        DIV,
        MOD,
        EQUAL,
        NOT_EQUAL,
        LESS,
        LESS_EQUAL,
        GREATER,
        GREATER_EQUAL,
        AND,
        OR,
    };
    Op op;
    ExprPtr left;
    ExprPtr right;
    BinaryExpr(Op op, ExprPtr left, ExprPtr right, SourceSector sector)
        : Expr(sector), op(op), left(std::move(left)), right(std::move(right))
    {
    }
    LIX_VISIT_EXPR(Binary)
};

const char* binary_op_to_string(BinaryExpr::Op op);

// 'target = value' or a compound assignment like 'target += value'. The target is a
// VarExpr, GetExpr or IndexExpr.
struct AssignExpr : Expr
{
    ExprPtr target;
    std::optional<BinaryExpr::Op> op;
    ExprPtr value;
    AssignExpr(ExprPtr target, std::optional<BinaryExpr::Op> op, ExprPtr value,
               SourceSector sector)
        : Expr(sector), target(std::move(target)), op(op), value(std::move(value))
    {
    }
    LIX_VISIT_EXPR(Assign)
};

// '++x', 'x++', '--x' and 'x--'.
struct IncrementExpr : Expr
{
    ExprPtr target;
    int delta;
    bool prefix;
    IncrementExpr(ExprPtr target, int delta, bool prefix, SourceSector sector)
        : Expr(sector), target(std::move(target)), delta(delta), prefix(prefix)
    {
    }
    LIX_VISIT_EXPR(Increment)
};

// 'f(a, b)', 'max<int>(a, b)', 'Point(1.0, 2.0)', 'vector<int>()' or 'obj.method(a)'.
// What is being called is only decided by the compiler.
struct CallExpr : Expr
{
    ExprPtr callee;
    std::vector<TypeExprPtr> type_args;
    std::vector<ExprPtr> args;
    CallExpr(ExprPtr callee, std::vector<TypeExprPtr> type_args,
             std::vector<ExprPtr> args, SourceSector sector)
        : Expr(sector), callee(std::move(callee)), type_args(std::move(type_args)),
          args(std::move(args))
    {
    }
    LIX_VISIT_EXPR(Call)
};

// 'object.name'
struct GetExpr : Expr
{
    ExprPtr object;
    std::string name;
    GetExpr(ExprPtr object, std::string name, SourceSector sector)
        : Expr(sector), object(std::move(object)), name(std::move(name))
    {
    }
    LIX_VISIT_EXPR(Get)
};

// 'object[index]'
struct IndexExpr : Expr
{
    ExprPtr object;
    ExprPtr index;
    IndexExpr(ExprPtr object, ExprPtr index, SourceSector sector)
        : Expr(sector), object(std::move(object)), index(std::move(index))
    {
    }
    LIX_VISIT_EXPR(Index)
};

// 'cast<float>(expr)'
struct CastExpr : Expr
{
    TypeExprPtr type;
    ExprPtr expr;
    CastExpr(TypeExprPtr type, ExprPtr expr, SourceSector sector)
        : Expr(sector), type(std::move(type)), expr(std::move(expr))
    {
    }
    LIX_VISIT_EXPR(Cast)
};

// '[1, 2, 3]'
struct VectorExpr : Expr
{
    std::vector<ExprPtr> elements;
    VectorExpr(std::vector<ExprPtr> elements, SourceSector sector)
        : Expr(sector), elements(std::move(elements))
    {
    }
    LIX_VISIT_EXPR(Vector)
};

#undef LIX_VISIT_EXPR
