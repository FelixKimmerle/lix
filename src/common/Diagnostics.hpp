#pragma once

#include "SourceSector.hpp"

#include <string>
#include <vector>

// Collects and prints compile errors in the familiar compiler format:
//
//   test.lix:3:9: error: cannot convert 'string' to 'int' in initializer of 'x'
//       3 | int x = "hello";
//         |         ^~~~~~~
class Diagnostics
{
  private:
    std::string file_name;
    std::vector<std::string> lines;
    int errors = 0;
    int muted = 0;

  public:
    Diagnostics(std::string file_name, const std::string& source);

    // The REPL feeds the session to one Diagnostics entry by entry. Lines are numbered
    // across the whole session, so an error inside a function declared in an earlier
    // entry still shows the right source line. Returns the number of the first new line.
    uint32_t add_source(const std::string& source);
    void reset_errors();

    void error(const SourceSector& sector, const std::string& message);
    bool had_error() const;
    int error_count() const;

    // While muted, errors are swallowed. The compiler uses this to compute the type of
    // an expression speculatively (see Compiler::probe_type).
    void mute();
    void unmute();
    int mute_level() const;
    void set_mute_level(int level);
};
