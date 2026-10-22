#include "Repl.hpp"

#include "common/Diagnostics.hpp"
#include "frontend/Parser.hpp"
#include "frontend/Scanner.hpp"

#include <cctype>
#include <cstdlib>
#include <iostream>

#ifdef LIX_HAVE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif

namespace
{
    struct InputState
    {
        int depth = 0; // open '(', '[' and '{'
        bool in_string = false;
        bool in_block_comment = false;
        char last_significant = '\0'; // last character outside of comments and whitespace
    };

    // Looks at an entry the way the scanner would, to decide whether it is complete.
    InputState analyze(const std::string& source)
    {
        InputState state;
        for (size_t i = 0; i < source.size(); i++)
        {
            char c = source[i];
            char next = i + 1 < source.size() ? source[i + 1] : '\0';
            if (state.in_block_comment)
            {
                if (c == '*' && next == '/')
                {
                    state.in_block_comment = false;
                    i++;
                }
                continue;
            }
            if (state.in_string)
            {
                if (c == '\\')
                {
                    i++;
                }
                else if (c == '"')
                {
                    state.in_string = false;
                    state.last_significant = c;
                }
                continue;
            }
            if (c == '/' && next == '/')
            {
                while (i < source.size() && source[i] != '\n')
                {
                    i++;
                }
                continue;
            }
            if (c == '/' && next == '*')
            {
                state.in_block_comment = true;
                i++;
                continue;
            }
            switch (c)
            {
            case '"':
                state.in_string = true;
                break;
            case '(':
            case '[':
            case '{':
                state.depth++;
                break;
            case ')':
            case ']':
            case '}':
                state.depth--;
                break;
            default:
                break;
            }
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            {
                state.last_significant = c;
            }
        }
        return state;
    }

    bool is_blank(const std::string& text)
    {
        return text.find_first_not_of(" \t\r\n") == std::string::npos;
    }

    std::string trim(const std::string& text)
    {
        size_t begin = text.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
        {
            return "";
        }
        size_t end = text.find_last_not_of(" \t\r\n");
        return text.substr(begin, end - begin + 1);
    }
} // namespace

Repl::Repl(std::istream& in, std::ostream& out, DebugOptions debug, VmOptions vm_options,
           bool interactive)
    : in(in), out(out), debug(debug), vm_options(vm_options), interactive(interactive)
{
}

bool Repl::read_line(const char* prompt, std::string& line)
{
    if (pushed_back_line)
    {
        line = std::move(*pushed_back_line);
        pushed_back_line.reset();
        return true;
    }
#ifdef LIX_HAVE_READLINE
    if (interactive)
    {
        char* input = readline(prompt);
        if (!input)
        {
            return false;
        }
        line = input;
        std::free(input);
        return true;
    }
#endif
    if (interactive)
    {
        out << prompt << std::flush;
    }
    return static_cast<bool>(std::getline(in, line));
}

namespace
{
    enum class Completeness
    {
        COMPLETE,        // parses (or has an error that more input cannot fix)
        MAYBE_ELSE,      // complete, but ends with an 'if' that an 'else' could continue
        NEEDS_SEMICOLON, // parses once a ';' is appended, like '1 + 2'
        INCOMPLETE,      // needs more lines, like 'int f()' before its '{'
    };

    std::string with_semicolon(const std::string& entry)
    {
        // On its own line only if the last line has a comment that would swallow it.
        size_t last_line = entry.rfind('\n');
        bool has_comment =
            entry.find("//", last_line == std::string::npos ? 0 : last_line) !=
            std::string::npos;
        return entry + (has_comment ? "\n;" : ";");
    }

    struct ParseResult
    {
        bool error;
        bool error_at_end;
        bool ends_with_if_without_else; // an 'else' on the next line would continue it
    };

    ParseResult try_parse(const std::string& source)
    {
        Diagnostics quiet("<repl>", "");
        quiet.mute();
        Scanner scanner(source, quiet);
        Parser parser(scanner.scan_tokens(), quiet);
        std::vector<StmtPtr> module = parser.parse();

        bool open_if = false;
        Stmt* last = module.empty() ? nullptr : module.back().get();
        while (auto* if_stmt = dynamic_cast<IfStmt*>(last))
        {
            open_if = !if_stmt->else_branch;
            last = if_stmt->else_branch.get();
        }
        return ParseResult{parser.had_error(), parser.had_error_at_end(), open_if};
    }

    // Like Python's REPL: try to parse the entry; if the only problem is that the input
    // ended too early, it is not finished yet.
    Completeness completeness(const std::string& entry)
    {
        InputState state = analyze(entry);
        if (state.depth > 0 || state.in_string || state.in_block_comment)
        {
            return Completeness::INCOMPLETE;
        }
        ParseResult raw = try_parse(entry);
        if (!raw.error && raw.ends_with_if_without_else)
        {
            return Completeness::MAYBE_ELSE;
        }
        if (!raw.error_at_end)
        {
            return Completeness::COMPLETE; // fine, or an error more input cannot fix
        }
        if (!try_parse(with_semicolon(entry)).error)
        {
            return Completeness::NEEDS_SEMICOLON;
        }
        return Completeness::INCOMPLETE;
    }
} // namespace

// Reads lines until the entry is complete. An empty line ends an entry early.
bool Repl::read_entry(std::string& entry)
{
    entry.clear();
    std::string line;
    if (!read_line("lix> ", line))
    {
        return false;
    }
    entry = line;
    if (!is_blank(entry) && trim(entry)[0] != ':')
    {
        for (;;)
        {
            Completeness state = completeness(entry);
            if (state == Completeness::NEEDS_SEMICOLON)
            {
                entry = with_semicolon(entry);
                break;
            }
            if (state == Completeness::COMPLETE)
            {
                break;
            }
            if (state == Completeness::MAYBE_ELSE)
            {
                // Peek at the next line: 'else' continues this entry, anything else is
                // the start of the next one.
                if (!read_line("...> ", line) || is_blank(line))
                {
                    break;
                }
                std::string next = trim(line);
                if (next.rfind("else", 0) == 0 &&
                    (next.size() == 4 ||
                     (!std::isalnum(static_cast<unsigned char>(next[4])) &&
                      next[4] != '_')))
                {
                    entry += "\n" + line;
                    continue;
                }
                pushed_back_line = line;
                break;
            }
            if (!read_line("...> ", line) || is_blank(line))
            {
                break; // compile what we have and report the error
            }
            entry += "\n" + line;
        }
    }
#ifdef LIX_HAVE_READLINE
    if (interactive && !is_blank(entry))
    {
        add_history(entry.c_str());
    }
#endif
    return true;
}

// Returns false if the REPL should exit.
bool Repl::handle_command(const std::string& command, Pipeline& pipeline)
{
    VmOptions options = pipeline.get_vm_options();
    auto toggle = [&](bool& flag, const char* what) {
        flag = !flag;
        out << what << ": " << (flag ? "on" : "off") << "\n";
    };

    if (command == ":quit" || command == ":q" || command == ":exit")
    {
        return false;
    }
    if (command == ":tokens")
    {
        toggle(pipeline.debug.tokens, "Printing tokens");
    }
    else if (command == ":ast")
    {
        toggle(pipeline.debug.ast, "Printing syntax trees");
    }
    else if (command == ":disasm")
    {
        toggle(pipeline.debug.disasm, "Printing bytecode");
    }
    else if (command == ":trace")
    {
        toggle(options.trace, "Tracing execution");
    }
    else if (command == ":stats")
    {
        toggle(pipeline.debug.stats, "Printing statistics");
    }
    else if (command == ":gc-stress")
    {
        toggle(options.gc_stress, "Collecting garbage on every allocation");
    }
    else if (command == ":help")
    {
        out << "Type Lix code to run it. Declarations stay available for later entries.\n"
               "Expressions print their value; a missing ';' at the end is added.\n"
               "An entry continues on the next line until it is complete; an empty line\n"
               "ends it early.\n"
               "\n"
               "  :tokens     toggle printing the tokens of each entry\n"
               "  :ast        toggle printing the syntax tree of each entry\n"
               "  :disasm     toggle printing the bytecode compiled for each entry\n"
               "  :trace      toggle tracing every instruction with the stack\n"
               "  :stats      toggle timings, code size and execution statistics\n"
               "  :gc-stress  toggle collecting garbage before every allocation\n"
               "  :help       show this help\n"
               "  :quit       exit (or press Ctrl-D)\n";
    }
    else
    {
        out << "Unknown command " << command << ". Type :help for help.\n";
    }
    pipeline.set_vm_options(options);
    return true;
}

int Repl::run()
{
    if (interactive)
    {
        out << "Lix REPL. Type :help for help, :quit or Ctrl-D to exit.\n";
    }

    Pipeline pipeline("<repl>", debug, vm_options, out, true);
    std::string entry;
    while (read_entry(entry))
    {
        std::string command = trim(entry);
        if (command.empty())
        {
            continue;
        }
        if (command[0] == ':')
        {
            if (!handle_command(command, pipeline))
            {
                break;
            }
            continue;
        }
        // Errors are reported by the pipeline; the session just continues.
        pipeline.run(entry);
    }

    if (interactive)
    {
        out << "\n";
    }
    return 0;
}
