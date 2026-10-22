#include "AstPrinter.hpp"

#include <sstream>

namespace
{
    std::string template_prefix(const std::vector<std::string>& type_params)
    {
        if (type_params.empty())
        {
            return "";
        }
        std::string result = "template<";
        for (size_t i = 0; i < type_params.size(); i++)
        {
            result += (i > 0 ? ", " : "") + type_params[i];
        }
        return result + "> ";
    }
} // namespace

AstPrinter::AstPrinter(std::ostream& out) : out(out)
{
}

void AstPrinter::print(const std::vector<StmtPtr>& module)
{
    for (const auto& stmt : module)
    {
        stmt->visit(this);
    }
}

void AstPrinter::line(const std::string& text)
{
    for (int i = 0; i < depth; i++)
    {
        out << "  ";
    }
    out << text << "\n";
}

void AstPrinter::child(Expr* expr)
{
    depth++;
    expr->visit(this);
    depth--;
}

void AstPrinter::child(Stmt* stmt)
{
    depth++;
    stmt->visit(this);
    depth--;
}

void AstPrinter::child(const std::string& label, Expr* expr)
{
    if (!expr)
    {
        return;
    }
    depth++;
    line(label + ":");
    child(expr);
    depth--;
}

void AstPrinter::child(const std::string& label, Stmt* stmt)
{
    if (!stmt)
    {
        return;
    }
    depth++;
    line(label + ":");
    child(stmt);
    depth--;
}

void AstPrinter::visitInt(IntExpr* expr)
{
    line("Int " + std::to_string(expr->value));
}

void AstPrinter::visitFloat(FloatExpr* expr)
{
    std::ostringstream stream;
    stream << expr->value;
    line("Float " + stream.str());
}

void AstPrinter::visitBool(BoolExpr* expr)
{
    line(expr->value ? "Bool true" : "Bool false");
}

void AstPrinter::visitString(StringExpr* expr)
{
    line("String \"" + expr->value + "\"");
}

void AstPrinter::visitNull(NullExpr*)
{
    line("Null");
}

void AstPrinter::visitThis(ThisExpr*)
{
    line("This");
}

void AstPrinter::visitVar(VarExpr* expr)
{
    line("Var " + expr->name);
}

void AstPrinter::visitUnary(UnaryExpr* expr)
{
    line(expr->op == UnaryExpr::Op::NEGATE ? "Unary -" : "Unary !");
    child(expr->operand.get());
}

void AstPrinter::visitBinary(BinaryExpr* expr)
{
    line(std::string("Binary ") + binary_op_to_string(expr->op));
    child(expr->left.get());
    child(expr->right.get());
}

void AstPrinter::visitAssign(AssignExpr* expr)
{
    line(std::string("Assign ") + (expr->op ? binary_op_to_string(*expr->op) : "") + "=");
    child(expr->target.get());
    child(expr->value.get());
}

void AstPrinter::visitIncrement(IncrementExpr* expr)
{
    std::string op = expr->delta > 0 ? "++" : "--";
    line(expr->prefix ? "Increment " + op + "x" : "Increment x" + op);
    child(expr->target.get());
}

void AstPrinter::visitCall(CallExpr* expr)
{
    std::string text = "Call";
    if (!expr->type_args.empty())
    {
        text += " <";
        for (size_t i = 0; i < expr->type_args.size(); i++)
        {
            text += (i > 0 ? ", " : "") + expr->type_args[i]->to_string();
        }
        text += ">";
    }
    line(text);
    child("callee", expr->callee.get());
    for (auto& arg : expr->args)
    {
        child("arg", arg.get());
    }
}

void AstPrinter::visitGet(GetExpr* expr)
{
    line("Get ." + expr->name);
    child(expr->object.get());
}

void AstPrinter::visitIndex(IndexExpr* expr)
{
    line("Index");
    child(expr->object.get());
    child(expr->index.get());
}

void AstPrinter::visitCast(CastExpr* expr)
{
    line("Cast<" + expr->type->to_string() + ">");
    child(expr->expr.get());
}

void AstPrinter::visitVector(VectorExpr* expr)
{
    line("Vector");
    for (auto& element : expr->elements)
    {
        child(element.get());
    }
}

void AstPrinter::visitExprStmt(ExprStmt* stmt)
{
    line("ExprStmt");
    child(stmt->expr.get());
}

void AstPrinter::visitPrint(PrintStmt* stmt)
{
    line("Print");
    child(stmt->expr.get());
}

void AstPrinter::visitVarStmt(VarStmt* stmt)
{
    line("VarDecl " + stmt->type->to_string() + " " + stmt->name);
    if (stmt->initializer)
    {
        child(stmt->initializer.get());
    }
}

void AstPrinter::visitBlock(BlockStmt* stmt)
{
    line("Block");
    for (auto& statement : stmt->statements)
    {
        child(statement.get());
    }
}

void AstPrinter::visitIf(IfStmt* stmt)
{
    line("If");
    child("condition", stmt->condition.get());
    child("then", stmt->then_branch.get());
    child("else", stmt->else_branch.get());
}

void AstPrinter::visitWhile(WhileStmt* stmt)
{
    line("While");
    child("condition", stmt->condition.get());
    child("body", stmt->body.get());
}

void AstPrinter::visitFor(ForStmt* stmt)
{
    line("For");
    child("initializer", stmt->initializer.get());
    child("condition", stmt->condition.get());
    child("increment", stmt->increment.get());
    child("body", stmt->body.get());
}

void AstPrinter::visitReturn(ReturnStmt* stmt)
{
    line("Return");
    if (stmt->value)
    {
        child(stmt->value.get());
    }
}

void AstPrinter::visitBreak(BreakStmt*)
{
    line("Break");
}

void AstPrinter::visitContinue(ContinueStmt*)
{
    line("Continue");
}

void AstPrinter::visitFuncDecl(FuncDecl* stmt)
{
    std::string text = template_prefix(stmt->type_params);
    text += stmt->is_constructor() ? "Constructor "
                                   : "Function " + stmt->return_type->to_string() + " ";
    text += stmt->name + "(";
    for (size_t i = 0; i < stmt->params.size(); i++)
    {
        text += (i > 0 ? ", " : "") + stmt->params[i].type->to_string() + " " +
                stmt->params[i].name;
    }
    line(text + ")");
    child(stmt->body.get());
}

void AstPrinter::visitClassDecl(ClassDecl* stmt)
{
    line(template_prefix(stmt->type_params) + "Class " + stmt->name);
    for (auto& field : stmt->fields)
    {
        child(field.get());
    }
    if (stmt->constructor)
    {
        child(stmt->constructor.get());
    }
    for (auto& method : stmt->methods)
    {
        child(method.get());
    }
}
