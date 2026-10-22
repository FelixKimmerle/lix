#include "Vm.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace
{
    // Integer arithmetic wraps around on overflow instead of being undefined behaviour.
    int64_t wrap_add(int64_t a, int64_t b)
    {
        return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
    }
    int64_t wrap_sub(int64_t a, int64_t b)
    {
        return static_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
    }
    int64_t wrap_mul(int64_t a, int64_t b)
    {
        return static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
    }
} // namespace

Vm::Vm(const Program& program, VmOptions options, std::ostream& out)
    : program(program), options(options), out(out), stack(STACK_MAX),
      stack_kinds(STACK_MAX)
{
    frames.reserve(FRAMES_MAX);
    sync_with_program();
}

void Vm::sync_with_program()
{
    globals.resize(program.global_kinds.size(), int_value(0));
    while (string_constants.size() < program.strings.size())
    {
        string_constants.push_back(
            new ObjString(program.strings[string_constants.size()]));
    }
}

Vm::~Vm()
{
    while (objects)
    {
        Obj* next = objects->next;
        delete objects;
        objects = next;
    }
    for (ObjString* string : string_constants)
    {
        delete string;
    }
}

Vm::Result Vm::run()
{
    return run(program.script);
}

void Vm::set_options(const VmOptions& new_options)
{
    options = new_options;
}

const VmOptions& Vm::get_options() const
{
    return options;
}

const VmStats& Vm::get_stats() const
{
    return stats;
}

void VmStats::print(std::ostream& os) const
{
    os << "  instructions executed  " << instructions << "\n"
       << "  function calls         " << calls << "\n"
       << "  max call depth         " << max_frames << "\n"
       << "  max stack slots        " << max_stack << "\n"
       << "  objects allocated      " << objects_allocated << "\n"
       << "  objects freed          " << objects_freed << "\n"
       << "  garbage collections    " << collections << "\n"
       << "  peak heap bytes        " << peak_heap_bytes << "\n";

    std::vector<std::pair<uint64_t, int>> counts;
    for (int op = 0; op < 256; op++)
    {
        if (opcode_counts[op] > 0)
        {
            counts.push_back({opcode_counts[op], op});
        }
    }
    std::sort(counts.rbegin(), counts.rend());
    os << "  instructions by opcode:\n";
    for (auto [count, op] : counts)
    {
        double percent = instructions ? 100.0 * static_cast<double>(count) /
                                            static_cast<double>(instructions)
                                      : 0;
        char line[96];
        std::snprintf(line, sizeof(line), "    %-20s %12llu  %5.1f%%\n",
                      opcode_name(static_cast<OpCode>(op)),
                      static_cast<unsigned long long>(count), percent);
        os << line;
    }
}

Vm::Result Vm::run(uint16_t function)
{
    sync_with_program();
    if (options.stats)
    {
        stats = VmStats();
    }
    sp = 0;
    frames.clear();
    try
    {
        return execute(function);
    }
    catch (const RuntimeError& runtime_error)
    {
        out.flush();
        std::cerr << "Runtime error: " << runtime_error.message << "\n";
        print_stack_trace(std::cerr);
        return Result::RUNTIME_ERROR;
    }
}

// ==================================================================================
// Stack
// ==================================================================================

inline void Vm::push(Value value, ValueKind kind)
{
    stack[sp] = value;
    stack_kinds[sp] = kind;
    sp++;
}

inline void Vm::push_ref(Obj* object)
{
    push(ref_value(object), ValueKind::REF);
}

inline Value Vm::pop()
{
    return stack[--sp];
}

inline Value Vm::peek(size_t distance) const
{
    return stack[sp - 1 - distance];
}

// ==================================================================================
// Errors
// ==================================================================================

void Vm::error(const std::string& message)
{
    throw RuntimeError{message};
}

void Vm::print_stack_trace(std::ostream& err) const
{
    // Innermost frame first. Deep recursion is shortened to its first and last frames.
    constexpr size_t SHOW_INNER = 10;
    constexpr size_t SHOW_OUTER = 5;
    size_t count = frames.size();
    for (size_t n = 0; n < count; n++)
    {
        if (count > SHOW_INNER + SHOW_OUTER && n == SHOW_INNER)
        {
            err << "  ... " << count - SHOW_INNER - SHOW_OUTER << " more frames ...\n";
            n = count - SHOW_OUTER - 1;
            continue;
        }
        const Frame& frame = frames[count - 1 - n];
        const Chunk& chunk = frame.function->chunk;
        // ip points behind the operands of the instruction that failed or called. Find
        // the start of that instruction by decoding the chunk from the beginning.
        size_t end = static_cast<size_t>(frame.ip - chunk.code.data());
        size_t offset = 0;
        for (size_t next = 0; next < end;)
        {
            offset = next;
            OpCode op = static_cast<OpCode>(chunk.code[next]);
            switch (opcode_format(op))
            {
            case OperandFormat::NONE:
                next += 1;
                break;
            case OperandFormat::BYTE:
            case OperandFormat::LOCAL:
            case OperandFormat::KIND:
                next += 2;
                break;
            default:
                next += 3;
                break;
            }
        }
        char position[24];
        std::snprintf(position, sizeof(position), "%04zu", offset);
        err << "  at " << frame.function->name << " (line " << chunk.lines[offset] << ", "
            << position << " " << opcode_name(static_cast<OpCode>(chunk.code[offset]))
            << ")\n";
    }
}

// ==================================================================================
// Garbage collection: precise mark and sweep
// ==================================================================================

size_t Vm::object_size(const Obj* object)
{
    switch (object->type)
    {
    case Obj::Type::STRING:
        return sizeof(ObjString) +
               static_cast<const ObjString*>(object)->value.capacity();
    case Obj::Type::INSTANCE:
        return sizeof(ObjInstance) +
               static_cast<const ObjInstance*>(object)->fields.size() * sizeof(Value);
    case Obj::Type::VECTOR:
        return sizeof(ObjVector) +
               static_cast<const ObjVector*>(object)->items.capacity() * sizeof(Value);
    }
    return 0;
}

template <class T, class... Args> T* Vm::allocate(Args&&... args)
{
    // Collect *before* creating the object: the new object cannot be reachable yet, and
    // the operands the caller still needs must be on the stack (peeked, not popped).
    if (options.gc_stress || bytes_allocated > next_gc)
    {
        collect_garbage();
    }
    T* object = new T(std::forward<Args>(args)...);
    object->next = objects;
    objects = object;
    bytes_allocated += object_size(object);
    if (options.stats)
    {
        stats.objects_allocated++;
        stats.peak_heap_bytes = std::max(stats.peak_heap_bytes, bytes_allocated);
    }
    return object;
}

ObjString* Vm::new_string(std::string value)
{
    return allocate<ObjString>(std::move(value));
}

void Vm::collect_garbage()
{
    for (size_t i = 0; i < sp; i++)
    {
        if (stack_kinds[i] == ValueKind::REF)
        {
            mark(stack[i].o);
        }
    }
    for (size_t i = 0; i < globals.size(); i++)
    {
        if (program.global_kinds[i] == ValueKind::REF)
        {
            mark(globals[i].o);
        }
    }
    while (!gray.empty())
    {
        Obj* object = gray.back();
        gray.pop_back();
        blacken(object);
    }
    sweep();
    stats.collections++;
    next_gc = std::max<size_t>(bytes_allocated * 2, 1024 * 1024);
}

void Vm::mark(Obj* object)
{
    if (object == nullptr || object->marked)
    {
        return;
    }
    object->marked = true;
    gray.push_back(object);
}

void Vm::blacken(Obj* object)
{
    switch (object->type)
    {
    case Obj::Type::STRING:
        break;
    case Obj::Type::INSTANCE: {
        auto* instance = static_cast<ObjInstance*>(object);
        for (size_t i = 0; i < instance->fields.size(); i++)
        {
            if (instance->klass->field_kinds[i] == ValueKind::REF)
            {
                mark(instance->fields[i].o);
            }
        }
        break;
    }
    case Obj::Type::VECTOR: {
        auto* vector = static_cast<ObjVector*>(object);
        if (vector->element_kind == ValueKind::REF)
        {
            for (Value item : vector->items)
            {
                mark(item.o);
            }
        }
        break;
    }
    }
}

void Vm::sweep()
{
    // String constants are not in this list, so they are never freed. They may stay
    // marked forever, which is harmless because strings have no children.
    Obj** link = &objects;
    bytes_allocated = 0;
    while (*link)
    {
        Obj* object = *link;
        if (object->marked)
        {
            object->marked = false;
            bytes_allocated += object_size(object);
            link = &object->next;
        }
        else
        {
            *link = object->next;
            delete object;
            stats.objects_freed++;
        }
    }
}

// ==================================================================================
// Printing
// ==================================================================================

void Vm::print_value(std::ostream& os, Value value, ValueKind kind, bool quote_strings,
                     int depth) const
{
    switch (kind)
    {
    case ValueKind::INT:
        os << value.i;
        return;
    case ValueKind::FLOAT:
        os << format_float(value.f);
        return;
    case ValueKind::BOOL:
        os << (value.b ? "true" : "false");
        return;
    case ValueKind::REF:
        break;
    }

    Obj* object = value.o;
    if (object == nullptr)
    {
        os << "null";
        return;
    }
    if (depth > 16)
    {
        os << "...";
        return;
    }
    switch (object->type)
    {
    case Obj::Type::STRING: {
        const std::string& string = static_cast<ObjString*>(object)->value;
        if (quote_strings)
        {
            os << '"' << string << '"';
        }
        else
        {
            os << string;
        }
        return;
    }
    case Obj::Type::VECTOR: {
        auto* vector = static_cast<ObjVector*>(object);
        os << "[";
        for (size_t i = 0; i < vector->items.size(); i++)
        {
            if (i > 0)
            {
                os << ", ";
            }
            print_value(os, vector->items[i], vector->element_kind, true, depth + 1);
        }
        os << "]";
        return;
    }
    case Obj::Type::INSTANCE: {
        auto* instance = static_cast<ObjInstance*>(object);
        os << instance->klass->name << "{";
        for (size_t i = 0; i < instance->fields.size(); i++)
        {
            if (i > 0)
            {
                os << ", ";
            }
            os << instance->klass->field_names[i] << ": ";
            print_value(os, instance->fields[i], instance->klass->field_kinds[i], true,
                        depth + 1);
        }
        os << "}";
        return;
    }
    }
}

// Prints the stack of the current frame, then the instruction that is about to run:
//
//   [main] stack: [ 3 ][ 2.5 ][ "hi" ]
//   0012    4 ADD_FLOAT
void Vm::trace_instruction(const Frame& frame, const uint8_t* ip) const
{
    std::cerr << "  " << std::string(2 * (frames.size() - 1), ' ') << "["
              << frame.function->name << "] stack:";
    for (size_t i = frame.base; i < sp; i++)
    {
        std::cerr << " [ ";
        print_value(std::cerr, stack[i], stack_kinds[i], true, 0);
        std::cerr << " ]";
    }
    std::cerr << "\n  " << std::string(2 * (frames.size() - 1), ' ');
    disassemble_instruction(program, frame.function->chunk,
                            static_cast<size_t>(ip - frame.function->chunk.code.data()),
                            std::cerr);
}

// ==================================================================================
// The interpreter loop
// ==================================================================================

Vm::Result Vm::execute(uint16_t function)
{
    const Function* script = program.functions[function].get();
    frames.push_back(Frame{script, script->chunk.code.data(), 0});

    Frame* frame = &frames.back();
    const uint8_t* ip = frame->ip;
    // Checked once per instruction, so tracing and statistics cost almost nothing when
    // they are off.
    const bool instrumented = options.trace || options.stats;

#define READ_BYTE() (*ip++)
#define READ_U16() (ip += 2, static_cast<uint16_t>(ip[-2] | (ip[-1] << 8)))
#define FAIL(message)                                                                    \
    do                                                                                   \
    {                                                                                    \
        frame->ip = ip;                                                                  \
        error(message);                                                                  \
    } while (false)
#define BINARY_INT(expr)                                                                 \
    do                                                                                   \
    {                                                                                    \
        int64_t b = stack[sp - 1].i;                                                     \
        int64_t a = stack[sp - 2].i;                                                     \
        sp--;                                                                            \
        stack[sp - 1] = int_value(expr);                                                 \
    } while (false)
#define BINARY_FLOAT(expr)                                                               \
    do                                                                                   \
    {                                                                                    \
        double b = stack[sp - 1].f;                                                      \
        double a = stack[sp - 2].f;                                                      \
        sp--;                                                                            \
        stack[sp - 1] = float_value(expr);                                               \
    } while (false)
#define COMPARE(member, op)                                                              \
    do                                                                                   \
    {                                                                                    \
        auto b = stack[sp - 1].member;                                                   \
        auto a = stack[sp - 2].member;                                                   \
        sp--;                                                                            \
        stack[sp - 1] = bool_value(a op b);                                              \
        stack_kinds[sp - 1] = ValueKind::BOOL;                                           \
    } while (false)

    for (;;)
    {
        if (instrumented)
        {
            if (options.trace)
            {
                trace_instruction(*frame, ip);
            }
            stats.instructions++;
            stats.opcode_counts[*ip]++;
            stats.max_stack = std::max(stats.max_stack, sp);
            stats.max_frames = std::max(stats.max_frames, frames.size());
        }

        OpCode op = static_cast<OpCode>(READ_BYTE());
        switch (op)
        {
        case OpCode::CONST: {
            uint16_t index = READ_U16();
            push(frame->function->chunk.constants[index],
                 frame->function->chunk.constant_kinds[index]);
        }
        break;
        case OpCode::CONST_STRING:
            push_ref(string_constants[READ_U16()]);
            break;
        case OpCode::TRUE:
            push(bool_value(true), ValueKind::BOOL);
            break;
        case OpCode::FALSE:
            push(bool_value(false), ValueKind::BOOL);
            break;
        case OpCode::NULL_REF:
            push_ref(nullptr);
            break;

        case OpCode::POP:
            sp--;
            break;
        case OpCode::POPN:
            sp -= READ_BYTE();
            break;
        case OpCode::DUP:
            push(stack[sp - 1], stack_kinds[sp - 1]);
            break;
        case OpCode::DUP2:
            push(stack[sp - 2], stack_kinds[sp - 2]);
            push(stack[sp - 2], stack_kinds[sp - 2]);
            break;

        case OpCode::GET_LOCAL: {
            size_t slot = frame->base + READ_BYTE();
            push(stack[slot], stack_kinds[slot]);
            break;
        }
        case OpCode::SET_LOCAL: {
            size_t slot = frame->base + READ_BYTE();
            stack[slot] = stack[sp - 1];
            stack_kinds[slot] = stack_kinds[sp - 1];
            break;
        }
        case OpCode::GET_GLOBAL: {
            uint16_t index = READ_U16();
            push(globals[index], program.global_kinds[index]);
            break;
        }
        case OpCode::SET_GLOBAL:
            globals[READ_U16()] = stack[sp - 1];
            break;
        case OpCode::GET_FIELD: {
            uint16_t index = READ_U16();
            auto* instance = static_cast<ObjInstance*>(stack[sp - 1].o);
            if (instance == nullptr)
            {
                FAIL("Null reference: cannot read a field of null.");
            }
            stack[sp - 1] = instance->fields[index];
            stack_kinds[sp - 1] = instance->klass->field_kinds[index];
            break;
        }
        case OpCode::SET_FIELD: {
            uint16_t index = READ_U16();
            auto* instance = static_cast<ObjInstance*>(stack[sp - 2].o);
            if (instance == nullptr)
            {
                FAIL("Null reference: cannot assign a field of null.");
            }
            Value value = stack[sp - 1];
            ValueKind kind = stack_kinds[sp - 1];
            instance->fields[index] = value;
            sp -= 2;
            push(value, kind);
            break;
        }

        case OpCode::ADD_INT:
            BINARY_INT(wrap_add(a, b));
            break;
        case OpCode::SUB_INT:
            BINARY_INT(wrap_sub(a, b));
            break;
        case OpCode::MUL_INT:
            BINARY_INT(wrap_mul(a, b));
            break;
        case OpCode::DIV_INT:
            if (stack[sp - 1].i == 0)
            {
                FAIL("Integer division by zero.");
            }
            // INT64_MIN / -1 overflows; wrap around like the other operations.
            BINARY_INT(b == -1 ? wrap_sub(0, a) : a / b);
            break;
        case OpCode::MOD_INT:
            if (stack[sp - 1].i == 0)
            {
                FAIL("Integer modulo by zero.");
            }
            BINARY_INT(b == -1 ? 0 : a % b);
            break;
        case OpCode::NEG_INT:
            stack[sp - 1] = int_value(wrap_sub(0, stack[sp - 1].i));
            break;

        case OpCode::ADD_FLOAT:
            BINARY_FLOAT(a + b);
            break;
        case OpCode::SUB_FLOAT:
            BINARY_FLOAT(a - b);
            break;
        case OpCode::MUL_FLOAT:
            BINARY_FLOAT(a * b);
            break;
        case OpCode::DIV_FLOAT:
            BINARY_FLOAT(a / b);
            break;
        case OpCode::NEG_FLOAT:
            stack[sp - 1] = float_value(-stack[sp - 1].f);
            break;

        case OpCode::CONCAT: {
            const std::string& a = static_cast<ObjString*>(peek(1).o)->value;
            const std::string& b = static_cast<ObjString*>(peek(0).o)->value;
            ObjString* result = new_string(a + b);
            sp -= 2;
            push_ref(result);
            break;
        }
        case OpCode::NOT:
            stack[sp - 1] = bool_value(!stack[sp - 1].b);
            break;

        case OpCode::EQ_INT:
            COMPARE(i, ==);
            break;
        case OpCode::LT_INT:
            COMPARE(i, <);
            break;
        case OpCode::LE_INT:
            COMPARE(i, <=);
            break;
        case OpCode::GT_INT:
            COMPARE(i, >);
            break;
        case OpCode::GE_INT:
            COMPARE(i, >=);
            break;
        case OpCode::EQ_FLOAT:
            COMPARE(f, ==);
            break;
        case OpCode::LT_FLOAT:
            COMPARE(f, <);
            break;
        case OpCode::LE_FLOAT:
            COMPARE(f, <=);
            break;
        case OpCode::GT_FLOAT:
            COMPARE(f, >);
            break;
        case OpCode::GE_FLOAT:
            COMPARE(f, >=);
            break;
        case OpCode::EQ_BOOL:
            COMPARE(b, ==);
            break;
        case OpCode::EQ_REF:
            COMPARE(o, ==);
            break;
#define COMPARE_STRING(op)                                                               \
    do                                                                                   \
    {                                                                                    \
        bool result = static_cast<ObjString*>(peek(1).o)                                 \
                          ->value op static_cast<ObjString*>(peek(0).o)                  \
                          ->value;                                                       \
        sp -= 2;                                                                         \
        push(bool_value(result), ValueKind::BOOL);                                       \
    } while (false)
        case OpCode::EQ_STRING:
            COMPARE_STRING(==);
            break;
        case OpCode::LT_STRING:
            COMPARE_STRING(<);
            break;
        case OpCode::LE_STRING:
            COMPARE_STRING(<=);
            break;
        case OpCode::GT_STRING:
            COMPARE_STRING(>);
            break;
        case OpCode::GE_STRING:
            COMPARE_STRING(>=);
            break;
#undef COMPARE_STRING

        case OpCode::INT_TO_FLOAT:
            stack[sp - 1] = float_value(static_cast<double>(stack[sp - 1].i));
            stack_kinds[sp - 1] = ValueKind::FLOAT;
            break;
        case OpCode::INT_TO_FLOAT_UNDER:
            stack[sp - 2] = float_value(static_cast<double>(stack[sp - 2].i));
            stack_kinds[sp - 2] = ValueKind::FLOAT;
            break;
        case OpCode::FLOAT_TO_INT: {
            double value = stack[sp - 1].f;
            // Converting NaN or an out of range float to an integer is undefined in C++.
            if (!(value >= -9223372036854775808.0 && value < 9223372036854775808.0))
            {
                FAIL("Cannot convert " + format_float(value) + " to int.");
            }
            stack[sp - 1] = int_value(static_cast<int64_t>(value));
            stack_kinds[sp - 1] = ValueKind::INT;
            break;
        }
        case OpCode::INT_TO_BOOL:
            stack[sp - 1] = bool_value(stack[sp - 1].i != 0);
            stack_kinds[sp - 1] = ValueKind::BOOL;
            break;
        case OpCode::BOOL_TO_INT:
            stack[sp - 1] = int_value(stack[sp - 1].b ? 1 : 0);
            stack_kinds[sp - 1] = ValueKind::INT;
            break;
        case OpCode::INT_TO_STRING: {
            ObjString* result = new_string(std::to_string(peek().i));
            sp--;
            push_ref(result);
            break;
        }
        case OpCode::FLOAT_TO_STRING: {
            ObjString* result = new_string(format_float(peek().f));
            sp--;
            push_ref(result);
            break;
        }
        case OpCode::BOOL_TO_STRING: {
            ObjString* result = new_string(peek().b ? "true" : "false");
            sp--;
            push_ref(result);
            break;
        }

        case OpCode::JUMP: {
            uint16_t offset = READ_U16();
            ip += offset;
            break;
        }
        case OpCode::JUMP_IF_FALSE: {
            uint16_t offset = READ_U16();
            if (!pop().b)
            {
                ip += offset;
            }
            break;
        }
        case OpCode::JUMP_IF_FALSE_KEEP: {
            uint16_t offset = READ_U16();
            if (!peek().b)
            {
                ip += offset;
            }
            break;
        }
        case OpCode::JUMP_IF_TRUE_KEEP: {
            uint16_t offset = READ_U16();
            if (peek().b)
            {
                ip += offset;
            }
            break;
        }
        case OpCode::LOOP: {
            uint16_t offset = READ_U16();
            ip -= offset;
            break;
        }

        case OpCode::CALL: {
            const Function* function = program.functions[READ_U16()].get();
            size_t base = sp - function->arity;
            if (function->is_method && stack[base].o == nullptr)
            {
                FAIL("Null reference: cannot call " + function->name + " on null.");
            }
            if (frames.size() >= FRAMES_MAX || sp + 1024 >= STACK_MAX)
            {
                FAIL("Stack overflow.");
            }
            frame->ip = ip;
            stats.calls++;
            frames.push_back(Frame{function, function->chunk.code.data(), base});
            frame = &frames.back();
            ip = frame->ip;
            break;
        }
        case OpCode::RETURN: {
            Value result = stack[sp - 1];
            ValueKind kind = stack_kinds[sp - 1];
            sp = frame->base;
            frames.pop_back();
            push(result, kind);
            frame = &frames.back();
            ip = frame->ip;
            break;
        }
        case OpCode::RETURN_VOID:
            sp = frame->base;
            frames.pop_back();
            if (frames.empty())
            {
                return Result::OK;
            }
            frame = &frames.back();
            ip = frame->ip;
            break;

        case OpCode::NEW: {
            const ClassInfo* klass = program.classes[READ_U16()].get();
            push_ref(allocate<ObjInstance>(klass));
            break;
        }
        case OpCode::VEC_NEW: {
            auto kind = static_cast<ValueKind>(READ_BYTE());
            push_ref(allocate<ObjVector>(kind));
            break;
        }
        case OpCode::VEC_PUSH: {
            auto* vector = static_cast<ObjVector*>(stack[sp - 2].o);
            vector->items.push_back(stack[sp - 1]);
            bytes_allocated += sizeof(Value);
            sp -= 2;
            break;
        }
        case OpCode::VEC_POP: {
            auto* vector = static_cast<ObjVector*>(stack[sp - 1].o);
            if (vector->items.empty())
            {
                FAIL("pop() on an empty vector.");
            }
            stack[sp - 1] = vector->items.back();
            stack_kinds[sp - 1] = vector->element_kind;
            vector->items.pop_back();
            break;
        }
        case OpCode::VEC_GET: {
            int64_t index = stack[sp - 1].i;
            auto* vector = static_cast<ObjVector*>(stack[sp - 2].o);
            if (index < 0 || static_cast<uint64_t>(index) >= vector->items.size())
            {
                FAIL("Vector index " + std::to_string(index) + " out of range (size " +
                     std::to_string(vector->items.size()) + ").");
            }
            sp--;
            stack[sp - 1] = vector->items[index];
            stack_kinds[sp - 1] = vector->element_kind;
            break;
        }
        case OpCode::VEC_SET: {
            Value value = stack[sp - 1];
            ValueKind kind = stack_kinds[sp - 1];
            int64_t index = stack[sp - 2].i;
            auto* vector = static_cast<ObjVector*>(stack[sp - 3].o);
            if (index < 0 || static_cast<uint64_t>(index) >= vector->items.size())
            {
                FAIL("Vector index " + std::to_string(index) + " out of range (size " +
                     std::to_string(vector->items.size()) + ").");
            }
            vector->items[index] = value;
            sp -= 3;
            push(value, kind);
            break;
        }
        case OpCode::VEC_LEN: {
            auto* vector = static_cast<ObjVector*>(stack[sp - 1].o);
            stack[sp - 1] = int_value(static_cast<int64_t>(vector->items.size()));
            stack_kinds[sp - 1] = ValueKind::INT;
            break;
        }
        case OpCode::VEC_CLEAR:
            static_cast<ObjVector*>(pop().o)->items.clear();
            break;
        case OpCode::STR_LEN: {
            auto* string = static_cast<ObjString*>(stack[sp - 1].o);
            stack[sp - 1] = int_value(static_cast<int64_t>(string->value.size()));
            stack_kinds[sp - 1] = ValueKind::INT;
            break;
        }

        case OpCode::PRINT_INT:
            out << pop().i << "\n";
            break;
        case OpCode::PRINT_FLOAT:
            out << format_float(pop().f) << "\n";
            break;
        case OpCode::PRINT_BOOL:
            out << (pop().b ? "true" : "false") << "\n";
            break;
        case OpCode::PRINT_REF:
            print_value(out, peek(), ValueKind::REF, false, 0);
            out << "\n";
            sp--;
            break;
        }
    }

#undef READ_BYTE
#undef READ_U16
#undef FAIL
#undef BINARY_INT
#undef BINARY_FLOAT
#undef COMPARE
}
