#pragma once

#include "common/SourceSector.hpp"

#include <memory>
#include <string>
#include <vector>

// A type as written in the source, e.g. 'int', 'Point' or 'vector<Box<float>>'.
//
// This is purely syntactic: the parser does not know which names are classes or
// template parameters. The compiler turns a TypeExpr into a real Type (see
// types/Type.hpp) once it knows what every name refers to. Keeping the two apart is
// what makes templates possible: the same TypeExpr 'T' resolves to 'int' in one
// instantiation and to 'vector<Point>' in another.
struct TypeExpr
{
    std::string name;
    std::vector<std::unique_ptr<TypeExpr>> args;
    SourceSector sector;

    std::string to_string() const;
};

using TypeExprPtr = std::unique_ptr<TypeExpr>;
