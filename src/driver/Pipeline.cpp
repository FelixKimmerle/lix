#include "Pipeline.hpp"

#include "ast/AstPrinter.hpp"
#include "frontend/Parser.hpp"
#include "frontend/Scanner.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>

namespace
{
    using Clock = std::chrono::steady_clock;

    double milliseconds(Clock::time_point from, Clock::time_point to)
    {
        return std::chrono::duration<double, std::milli>(to - from).count();
    }

    void print_time(const char* stage, double ms)
    {
        char line[64];
        std::snprintf(line, sizeof(line), "  %-22s %10.3f ms\n", stage, ms);
        std::cerr << line;
    }
} // namespace

Pipeline::Pipeline(const std::string& name, DebugOptions debug, VmOptions vm_options,
                   std::ostream& out, bool repl_mode)
    : debug(debug), out(out), diagnostics(name, ""), compiler(diagnostics),
      vm(compiler.get_program(), vm_options, out)
{
    compiler.set_repl_mode(repl_mode);
    set_vm_options(vm_options);
}

void Pipeline::set_vm_options(const VmOptions& options)
{
    vm.set_options(options);
}

const VmOptions& Pipeline::get_vm_options() const
{
    return vm.get_options();
}

Pipeline::Result Pipeline::run(const std::string& source, bool execute)
{
    Program& program = compiler.get_program();
    auto start = Clock::now();

    // ---- scan ----
    diagnostics.reset_errors();
    uint32_t first_line = diagnostics.add_source(source);
    Scanner scanner(source, diagnostics, first_line);
    std::vector<Token> tokens = scanner.scan_tokens();
    auto scanned = Clock::now();
    if (debug.tokens)
    {
        out << "== tokens ==\n";
        for (const Token& token : tokens)
        {
            out << token << "\n";
        }
    }

    // ---- parse ----
    Parser parser(std::move(tokens), diagnostics);
    std::vector<StmtPtr> module = parser.parse();
    auto parsed = Clock::now();
    if (diagnostics.had_error())
    {
        return Result::COMPILE_ERROR;
    }
    if (module.empty())
    {
        return Result::OK;
    }
    if (debug.ast)
    {
        out << "== syntax tree ==\n";
        AstPrinter(out).print(module);
    }

    // ---- compile ----
    // Remember the code size of every function, to show only what this run compiled.
    // That includes template members of earlier entries that were compiled lazily now.
    std::vector<size_t> code_sizes;
    for (const auto& function : program.functions)
    {
        code_sizes.push_back(function->chunk.code.size());
    }
    std::optional<uint16_t> script = compiler.compile(module);
    modules.push_back(std::move(module));
    auto compiled = Clock::now();
    if (!script)
    {
        return Result::COMPILE_ERROR;
    }

    size_t new_functions = 0;
    size_t new_bytes = 0;
    if (debug.disasm)
    {
        out << "== bytecode ==\n";
    }
    for (size_t i = 0; i < program.functions.size(); i++)
    {
        const Function& function = *program.functions[i];
        size_t before = i < code_sizes.size() ? code_sizes[i] : 0;
        if (function.chunk.code.size() == before)
        {
            continue; // unchanged, or a template member that was never used
        }
        new_functions++;
        new_bytes += function.chunk.code.size() - before;
        if (debug.disasm)
        {
            disassemble_function(program, function, out);
            out << "\n";
        }
    }

    if (!execute)
    {
        return Result::OK;
    }

    // ---- run ----
    VmOptions options = vm.get_options();
    options.stats = debug.stats;
    vm.set_options(options);
    out.flush();
    Vm::Result result = vm.run(*script);
    out.flush();
    auto finished = Clock::now();

    if (debug.stats)
    {
        std::cerr << "== stats ==\n";
        print_time("scan", milliseconds(start, scanned));
        print_time("parse", milliseconds(scanned, parsed));
        print_time("compile", milliseconds(parsed, compiled));
        print_time("run", milliseconds(compiled, finished));
        std::cerr << "  functions compiled     " << new_functions << "\n"
                  << "  bytecode bytes         " << new_bytes << "\n";
        vm.get_stats().print(std::cerr);
    }
    return result == Vm::Result::OK ? Result::OK : Result::RUNTIME_ERROR;
}
