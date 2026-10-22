#include "Expr.hpp"
#include "TypeExpr.hpp"

std::string TypeExpr::to_string() const
{
    std::string result = name;
    if (!args.empty())
    {
        result += "<";
        for (size_t i = 0; i < args.size(); i++)
        {
            if (i > 0)
            {
                result += ", ";
            }
            result += args[i]->to_string();
        }
        result += ">";
    }
    return result;
}

const char* binary_op_to_string(BinaryExpr::Op op)
{
    switch (op)
    {
    case BinaryExpr::Op::ADD:
        return "+";
    case BinaryExpr::Op::SUB:
        return "-";
    case BinaryExpr::Op::MUL:
        return "*";
    case BinaryExpr::Op::DIV:
        return "/";
    case BinaryExpr::Op::MOD:
        return "%";
    case BinaryExpr::Op::EQUAL:
        return "==";
    case BinaryExpr::Op::NOT_EQUAL:
        return "!=";
    case BinaryExpr::Op::LESS:
        return "<";
    case BinaryExpr::Op::LESS_EQUAL:
        return "<=";
    case BinaryExpr::Op::GREATER:
        return ">";
    case BinaryExpr::Op::GREATER_EQUAL:
        return ">=";
    case BinaryExpr::Op::AND:
        return "&&";
    case BinaryExpr::Op::OR:
        return "||";
    }
    return "?";
}
