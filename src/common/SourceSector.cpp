#include "SourceSector.hpp"

bool SourcePosition::operator<(const SourcePosition& other) const
{
    return line < other.line || (line == other.line && column < other.column);
}

SourceSector::SourceSector(SourcePosition begin, SourcePosition end)
    : begin(begin), end(end), valid(true)
{
}

SourceSector SourceSector::operator|(const SourceSector& other) const
{
    if (!valid)
    {
        return other;
    }
    if (!other.valid)
    {
        return *this;
    }
    SourcePosition new_begin = other.begin < begin ? other.begin : begin;
    SourcePosition new_end = end < other.end ? other.end : end;
    return SourceSector(new_begin, new_end);
}

SourceSector& SourceSector::operator|=(const SourceSector& other)
{
    *this = *this | other;
    return *this;
}

std::ostream& operator<<(std::ostream& out, const SourceSector& sector)
{
    return out << sector.begin.line + 1 << ":" << sector.begin.column + 1;
}
