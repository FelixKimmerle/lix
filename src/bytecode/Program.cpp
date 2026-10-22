#include "Program.hpp"

#include <cmath>
#include <cstdio>
#include <iomanip>

const char* opcode_name(OpCode op)
{
    switch (op)
    {
#define LIX_OPCODE_NAME(name, format)                                                    \
    case OpCode::name:                                                                   \
        return #name;
        LIX_OPCODES(LIX_OPCODE_NAME)
#undef LIX_OPCODE_NAME
    }
    return "UNKNOWN";
}

OperandFormat opcode_format(OpCode op)
{
    switch (op)
    {
#define LIX_OPCODE_FORMAT(name, format)                                                  \
    case OpCode::name:                                                                   \
        return OperandFormat::format;
        LIX_OPCODES(LIX_OPCODE_FORMAT)
#undef LIX_OPCODE_FORMAT
    }
    return OperandFormat::NONE;
}

std::string format_float(double value)
{
    if (std::isnan(value))
    {
        return "nan";
    }
    if (std::isinf(value))
    {
        return value > 0 ? "inf" : "-inf";
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.15g", value);
    std::string result = buffer;
    // Always show that a float is a float: 3.0 instead of 3.
    if (result.find_first_of(".e") == std::string::npos)
    {
        result += ".0";
    }
    return result;
}

void Chunk::write(uint8_t byte, uint32_t line)
{
    code.push_back(byte);
    lines.push_back(line);
}

void Chunk::write(OpCode op, uint32_t line)
{
    write(static_cast<uint8_t>(op), line);
}

void Chunk::write_u16(uint16_t value, uint32_t line)
{
    write(static_cast<uint8_t>(value & 0xff), line);
    write(static_cast<uint8_t>(value >> 8), line);
}

uint16_t Chunk::read_u16(size_t offset) const
{
    return static_cast<uint16_t>(code[offset] | (code[offset + 1] << 8));
}

void disassemble(const Program& program, std::ostream& out)
{
    for (const auto& function : program.functions)
    {
        disassemble_function(program, *function, out);
        out << "\n";
    }
}

void disassemble_function(const Program& program, const Function& function,
                          std::ostream& out)
{
    out << "== " << function.name << " (arity " << function.arity << ") ==\n";
    for (size_t offset = 0; offset < function.chunk.code.size();)
    {
        offset = disassemble_instruction(program, function.chunk, offset, out);
    }
}

namespace
{
    const char* kind_name(ValueKind kind)
    {
        switch (kind)
        {
        case ValueKind::INT:
            return "int";
        case ValueKind::FLOAT:
            return "float";
        case ValueKind::BOOL:
            return "bool";
        case ValueKind::REF:
            return "ref";
        }
        return "?";
    }
} // namespace

size_t disassemble_instruction(const Program& program, const Chunk& chunk, size_t offset,
                               std::ostream& out)
{
    out << std::setw(4) << std::setfill('0') << offset << std::setfill(' ') << " ";
    if (offset > 0 && chunk.lines[offset] == chunk.lines[offset - 1])
    {
        out << "   | ";
    }
    else
    {
        out << std::setw(4) << chunk.lines[offset] << " ";
    }

    OpCode op = static_cast<OpCode>(chunk.code[offset]);
    out << std::left << std::setw(20) << opcode_name(op) << std::right;

    switch (opcode_format(op))
    {
    case OperandFormat::NONE:
        out << "\n";
        return offset + 1;
    case OperandFormat::BYTE:
    case OperandFormat::LOCAL:
        out << static_cast<int>(chunk.code[offset + 1]) << "\n";
        return offset + 2;
    case OperandFormat::KIND:
        out << kind_name(static_cast<ValueKind>(chunk.code[offset + 1])) << "\n";
        return offset + 2;
    case OperandFormat::CONSTANT: {
        uint16_t index = chunk.read_u16(offset + 1);
        Value value = chunk.constants[index];
        out << index << " (";
        if (chunk.constant_kinds[index] == ValueKind::FLOAT)
        {
            out << format_float(value.f);
        }
        else
        {
            out << value.i;
        }
        out << ")\n";
        return offset + 3;
    }
    case OperandFormat::STRING: {
        uint16_t index = chunk.read_u16(offset + 1);
        out << index << " \"" << program.strings[index] << "\"\n";
        return offset + 3;
    }
    case OperandFormat::GLOBAL: {
        uint16_t index = chunk.read_u16(offset + 1);
        out << index << " (" << program.global_names[index] << ")\n";
        return offset + 3;
    }
    case OperandFormat::FIELD:
        out << chunk.read_u16(offset + 1) << "\n";
        return offset + 3;
    case OperandFormat::FUNCTION: {
        uint16_t index = chunk.read_u16(offset + 1);
        out << index << " (" << program.functions[index]->name << ")\n";
        return offset + 3;
    }
    case OperandFormat::CLASS: {
        uint16_t index = chunk.read_u16(offset + 1);
        out << index << " (" << program.classes[index]->name << ")\n";
        return offset + 3;
    }
    case OperandFormat::JUMP: {
        uint16_t jump = chunk.read_u16(offset + 1);
        out << "-> " << offset + 3 + jump << "\n";
        return offset + 3;
    }
    case OperandFormat::LOOP: {
        uint16_t jump = chunk.read_u16(offset + 1);
        out << "-> " << offset + 3 - jump << "\n";
        return offset + 3;
    }
    }
    out << "\n";
    return offset + 1;
}
