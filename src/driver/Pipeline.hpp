#pragma once

#include "ast/Stmt.hpp"
#include "common/Diagnostics.hpp"
#include "compiler/Compiler.hpp"
#include "vm/Vm.hpp"

#include <ostream>
#include <string>
#include <vector>

// What to show of each stage. Dumps of tokens, syntax trees and bytecode go to the
// normal output so they can be redirected into a file; the trace and the statistics go
// to stderr because they are interleaved with the program's own output.
struct DebugOptions
{
    bool tokens = false; // after scanning: every token
    bool ast = false;    // after parsing: the syntax tree
    bool disasm = false; // after compiling: the bytecode of every newly compiled function
    bool stats = false;  // after running: time per stage, code size and VmStats
};

// The whole way from source text to execution: scanner, parser, compiler and VM, with
// the debug output of each stage. Used for files (one run) and by the REPL (one run per
// entry, sharing the compiler and the VM so declarations and globals persist).
class Pipeline
{
  public:
    enum class Result
    {
        OK,
        COMPILE_ERROR,
        RUNTIME_ERROR,
    };

    Pipeline(const std::string& name, DebugOptions debug, VmOptions vm_options,
             std::ostream& out, bool repl_mode);

    Result run(const std::string& source, bool execute = true);

    DebugOptions debug;
    void set_vm_options(const VmOptions& options);
    const VmOptions& get_vm_options() const;

  private:
    std::ostream& out;
    Diagnostics diagnostics;
    Compiler compiler;
    Vm vm;
    // The compiler keeps pointers into earlier syntax trees, so they stay alive.
    std::vector<std::vector<StmtPtr>> modules;
};
