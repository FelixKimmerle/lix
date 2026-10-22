#pragma once

#include "driver/Pipeline.hpp"
#include "vm/Vm.hpp"

#include <istream>
#include <optional>
#include <ostream>
#include <string>

// Interactive read-eval-print loop.
//
// Every entry is scanned, parsed and compiled on its own, into the *same* Compiler and
// Program, and then run on the *same* Vm. So classes, functions and globals declared in
// one entry stay available in the next ones, and so do the values of the globals.
// An entry that fails to compile is rolled back completely (see Compiler::compile).
//
// An entry may span several lines: input is read until all brackets are closed. A
// missing ';' at the end is added, and a top-level expression prints its value, so
// typing '1 + 2' prints 3.
class Repl
{
  private:
    std::istream& in;
    std::ostream& out;
    DebugOptions debug;
    VmOptions vm_options;
    bool interactive;
    std::optional<std::string>
        pushed_back_line; // read ahead after an 'if', see read_entry

    bool read_entry(std::string& entry);
    bool read_line(const char* prompt, std::string& line);
    bool handle_command(const std::string& command, Pipeline& pipeline);

  public:
    Repl(std::istream& in, std::ostream& out, DebugOptions debug, VmOptions vm_options,
         bool interactive);
    int run();
};
