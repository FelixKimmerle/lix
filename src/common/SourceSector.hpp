#pragma once

#include <cstdint>
#include <ostream>

// A position inside a source file. Line and column are zero based internally and
// printed one based.
struct SourcePosition
{
    uint32_t line = 0;
    uint32_t column = 0;

    bool operator<(const SourcePosition& other) const;
};

// A range [begin, end) inside a source file. Every token, AST node and type expression
// carries one so that errors can point at the exact piece of code.
class SourceSector
{
  public:
    SourcePosition begin;
    SourcePosition end;
    bool valid = false;

    SourceSector() = default;
    SourceSector(SourcePosition begin, SourcePosition end);

    // Smallest sector that covers both sectors.
    SourceSector operator|(const SourceSector& other) const;
    SourceSector& operator|=(const SourceSector& other);
};

std::ostream& operator<<(std::ostream& out, const SourceSector& sector);
