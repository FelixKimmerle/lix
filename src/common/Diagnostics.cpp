#include "Diagnostics.hpp"

#include <iostream>

Diagnostics::Diagnostics(std::string file_name, const std::string& source)
    : file_name(std::move(file_name))
{
    if (!source.empty())
    {
        add_source(source);
    }
}

uint32_t Diagnostics::add_source(const std::string& source)
{
    // Split exactly like the scanner counts lines: on every '\n'.
    uint32_t first_line = static_cast<uint32_t>(lines.size());
    size_t start = 0;
    for (;;)
    {
        size_t end = source.find('\n', start);
        if (end == std::string::npos)
        {
            lines.push_back(source.substr(start));
            return first_line;
        }
        lines.push_back(source.substr(start, end - start));
        start = end + 1;
    }
}

void Diagnostics::reset_errors()
{
    errors = 0;
}

void Diagnostics::error(const SourceSector& sector, const std::string& message)
{
    if (muted > 0)
    {
        return;
    }
    errors++;

    std::cerr << file_name << ":" << sector << ": error: " << message << "\n";
    if (!sector.valid || sector.begin.line >= lines.size())
    {
        return;
    }

    const std::string& line = lines[sector.begin.line];
    std::string line_number = std::to_string(sector.begin.line + 1);
    std::cerr << " " << line_number << " | " << line << "\n";
    std::cerr << " " << std::string(line_number.size(), ' ') << " | ";

    // Reproduce tabs so the caret lines up with the source line.
    for (uint32_t i = 0; i < sector.begin.column && i < line.size(); i++)
    {
        std::cerr << (line[i] == '\t' ? '\t' : ' ');
    }
    uint32_t end_column = sector.end.line == sector.begin.line
                              ? sector.end.column
                              : static_cast<uint32_t>(line.size());
    std::cerr << "^";
    for (uint32_t i = sector.begin.column + 1; i < end_column; i++)
    {
        std::cerr << "~";
    }
    std::cerr << "\n";
}

bool Diagnostics::had_error() const
{
    return errors > 0;
}

int Diagnostics::error_count() const
{
    return errors;
}

void Diagnostics::mute()
{
    muted++;
}

void Diagnostics::unmute()
{
    muted--;
}

int Diagnostics::mute_level() const
{
    return muted;
}

void Diagnostics::set_mute_level(int level)
{
    muted = level;
}
