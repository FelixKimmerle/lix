#include "driver/Pipeline.hpp"
#include "repl/Repl.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace
{
    // Exit codes, following the BSD sysexits convention like clox does.
    constexpr int EXIT_USAGE = 64;
    constexpr int EXIT_COMPILE_ERROR = 65;
    constexpr int EXIT_RUNTIME_ERROR = 70;
    constexpr int EXIT_IO_ERROR = 74;

    void usage()
    {
        std::cerr
            << "Usage: lix [options] [file.lix]\n"
               "\n"
               "Without a file, lix starts an interactive REPL (the options work there\n"
               "too, and can be toggled with :tokens, :ast, :disasm, :trace, :stats).\n"
               "\n"
               "Options:\n"
               "  --tokens      print the tokens produced by the scanner\n"
               "  --ast         print the syntax tree produced by the parser\n"
               "  --disasm      print the bytecode produced by the compiler\n"
               "  --trace       print every executed instruction with the stack "
               "(stderr)\n"
               "  --stats       print timings, code size and execution statistics "
               "(stderr)\n"
               "  --gc-stress   run the garbage collector before every allocation\n"
               "  --no-run      stop after compiling\n";
    }
} // namespace

int main(int argc, char** argv)
{
    DebugOptions debug;
    VmOptions vm_options;
    bool run = true;
    std::string path;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--tokens")
        {
            debug.tokens = true;
        }
        else if (arg == "--ast")
        {
            debug.ast = true;
        }
        else if (arg == "--disasm")
        {
            debug.disasm = true;
        }
        else if (arg == "--stats")
        {
            debug.stats = true;
        }
        else if (arg == "--trace")
        {
            vm_options.trace = true;
        }
        else if (arg == "--gc-stress")
        {
            vm_options.gc_stress = true;
        }
        else if (arg == "--no-run")
        {
            run = false;
        }
        else if (arg == "-h" || arg == "--help")
        {
            usage();
            return 0;
        }
        else if (!arg.empty() && arg[0] == '-')
        {
            std::cerr << "Unknown option " << arg << "\n";
            usage();
            return EXIT_USAGE;
        }
        else if (path.empty())
        {
            path = arg;
        }
        else
        {
            usage();
            return EXIT_USAGE;
        }
    }

    if (path.empty())
    {
        return Repl(std::cin, std::cout, debug, vm_options, isatty(STDIN_FILENO)).run();
    }

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        std::cerr << "Could not open file '" << path << "'.\n";
        return EXIT_IO_ERROR;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    Pipeline pipeline(path, debug, vm_options, std::cout, false);
    switch (pipeline.run(buffer.str(), run))
    {
    case Pipeline::Result::OK:
        return 0;
    case Pipeline::Result::COMPILE_ERROR:
        return EXIT_COMPILE_ERROR;
    case Pipeline::Result::RUNTIME_ERROR:
        return EXIT_RUNTIME_ERROR;
    }
    return 0;
}
