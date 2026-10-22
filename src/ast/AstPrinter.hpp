#pragma once

#include "Expr.hpp"
#include "Stmt.hpp"

#include <ostream>
#include <vector>

// Prints the syntax tree, one node per line, indented by depth. Used by 'lix --ast'.
class AstPrinter : public VisitorExpr, public VisitorStmt
{
  private:
    std::ostream& out;
    int depth = 0;

    void line(const std::string& text);
    void child(Expr* expr);
    void child(Stmt* stmt);
    void child(const std::string& label, Expr* expr);
    void child(const std::string& label, Stmt* stmt);

  public:
    explicit AstPrinter(std::ostream& out);
    void print(const std::vector<StmtPtr>& module);

    void visitInt(IntExpr* expr) override;
    void visitFloat(FloatExpr* expr) override;
    void visitBool(BoolExpr* expr) override;
    void visitString(StringExpr* expr) override;
    void visitNull(NullExpr* expr) override;
    void visitThis(ThisExpr* expr) override;
    void visitVar(VarExpr* expr) override;
    void visitUnary(UnaryExpr* expr) override;
    void visitBinary(BinaryExpr* expr) override;
    void visitAssign(AssignExpr* expr) override;
    void visitIncrement(IncrementExpr* expr) override;
    void visitCall(CallExpr* expr) override;
    void visitGet(GetExpr* expr) override;
    void visitIndex(IndexExpr* expr) override;
    void visitCast(CastExpr* expr) override;
    void visitVector(VectorExpr* expr) override;

    void visitExprStmt(ExprStmt* stmt) override;
    void visitPrint(PrintStmt* stmt) override;
    void visitVarStmt(VarStmt* stmt) override;
    void visitBlock(BlockStmt* stmt) override;
    void visitIf(IfStmt* stmt) override;
    void visitWhile(WhileStmt* stmt) override;
    void visitFor(ForStmt* stmt) override;
    void visitReturn(ReturnStmt* stmt) override;
    void visitBreak(BreakStmt* stmt) override;
    void visitContinue(ContinueStmt* stmt) override;
    void visitFuncDecl(FuncDecl* stmt) override;
    void visitClassDecl(ClassDecl* stmt) override;
};
