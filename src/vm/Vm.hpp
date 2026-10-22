#pragma once

#include "Object.hpp"
#include "bytecode/Program.hpp"

#include <ostream>
#include <string>
#include <vector>

struct VmOptions
{
    bool trace = false;     // print every instruction and the stack while running
    bool gc_stress = false; // collect garbage before every allocation
    bool stats = false;     // count instructions, calls and GC work (see VmStats)
};

// Collected while running when VmOptions::stats is set. Useful to measure the effect
// of an optimization: fewer instructions, fewer calls, less garbage.
struct VmStats
{
    uint64_t instructions = 0;
    uint64_t opcode_counts[256] = {};
    uint64_t calls = 0;
    size_t max_frames = 0;
    size_t max_stack = 0;
    uint64_t collections = 0;
    uint64_t objects_allocated = 0;
    uint64_t objects_freed = 0;
    size_t peak_heap_bytes = 0;

    void print(std::ostream& os) const;
};

// Stack based virtual machine.
//
// Every value is one 8 byte slot. Next to the value stack there is a parallel array
// that records which slots hold heap references. The instructions are statically
// typed, so every instruction knows whether it pushes a reference, and the garbage
// collector can find all live objects precisely without tagging every value.
class Vm
{
  public:
    enum class Result
    {
        OK,
        RUNTIME_ERROR,
    };

    Vm(const Program& program, VmOptions options, std::ostream& out);
    ~Vm();
    Vm(const Vm&) = delete;
    Vm& operator=(const Vm&) = delete;

    // Runs the program's script function.
    Result run();
    // Runs one function without arguments, e.g. the code of a REPL entry. Heap and
    // globals are kept between runs, and globals or string constants that were added to
    // the program since the last run are picked up.
    Result run(uint16_t function);

    void set_options(const VmOptions& new_options);
    const VmOptions& get_options() const;
    // Statistics of the last run (reset by every run when stats are enabled).
    const VmStats& get_stats() const;

  private:
    struct Frame
    {
        const Function* function;
        const uint8_t* ip;
        size_t base;
    };

    struct RuntimeError
    {
        std::string message;
    };

    static constexpr size_t STACK_MAX = 1 << 16;
    static constexpr size_t FRAMES_MAX = 4096;

    const Program& program;
    VmOptions options;
    std::ostream& out;

    std::vector<Value> stack;
    // The kind of every stack slot. The GC needs to know which slots are references;
    // --trace uses the rest to print values with their real types.
    std::vector<ValueKind> stack_kinds;
    size_t sp = 0;
    std::vector<Frame> frames;
    std::vector<Value> globals;
    std::vector<ObjString*> string_constants; // never collected

    Obj* objects = nullptr;
    size_t bytes_allocated = 0;
    size_t next_gc = 1024 * 1024;
    std::vector<Obj*> gray;
    VmStats stats;

    Result execute(uint16_t function);
    void sync_with_program();

    void push(Value value, ValueKind kind);
    void push_ref(Obj* object);
    Value pop();
    Value peek(size_t distance = 0) const;

    template <class T, class... Args> T* allocate(Args&&... args);
    ObjString* new_string(std::string value);
    void collect_garbage();
    void mark(Obj* object);
    void blacken(Obj* object);
    void sweep();
    static size_t object_size(const Obj* object);

    [[noreturn]] void error(const std::string& message);
    void print_stack_trace(std::ostream& err) const;
    void trace_instruction(const Frame& frame, const uint8_t* ip) const;
    void print_value(std::ostream& os, Value value, ValueKind kind, bool quote_strings,
                     int depth) const;
};
