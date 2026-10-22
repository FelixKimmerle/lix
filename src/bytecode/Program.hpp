#pragma once

#include "OpCode.hpp"
#include "Value.hpp"

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

class Chunk
{
  public:
    std::vector<uint8_t> code;
    std::vector<uint32_t> lines; // source line of every byte in 'code'
    std::vector<Value> constants;
    std::vector<ValueKind> constant_kinds; // only used by the disassembler

    void write(uint8_t byte, uint32_t line);
    void write(OpCode op, uint32_t line);
    void write_u16(uint16_t value, uint32_t line);
    uint16_t read_u16(size_t offset) const;
};

struct Function
{
    std::string name;
    uint16_t arity = 0;     // number of parameter slots, including 'this' for methods
    bool is_method = false; // the VM null-checks slot 0 when calling methods
    Chunk chunk;
};

// Runtime description of a class instantiation, e.g. 'Box<int>'.
struct ClassInfo
{
    std::string name;
    std::vector<std::string> field_names;
    std::vector<ValueKind> field_kinds;
};

// Everything the VM needs to run a compiled Lix program.
struct Program
{
    std::vector<std::unique_ptr<Function>> functions;
    std::vector<std::unique_ptr<ClassInfo>>
        classes;                      // stable addresses: objects point here
    std::vector<std::string> strings; // string constants
    std::vector<std::string> global_names;
    std::vector<ValueKind> global_kinds;
    uint16_t script = 0; // the function holding the top level statements
};

// Formats a float the way Lix prints it: always with a decimal point or exponent.
std::string format_float(double value);

void disassemble(const Program& program, std::ostream& out);
void disassemble_function(const Program& program, const Function& function,
                          std::ostream& out);
size_t disassemble_instruction(const Program& program, const Chunk& chunk, size_t offset,
                               std::ostream& out);
