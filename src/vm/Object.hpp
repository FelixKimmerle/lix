#pragma once

#include "bytecode/Program.hpp"
#include "bytecode/Value.hpp"

#include <string>
#include <vector>

// Heap objects. Strings, vectors and class instances live on the heap and are managed
// by the garbage collector in Vm. A Value of a reference type is a pointer to one of
// these (or nullptr for 'null').
struct Obj
{
    enum class Type : uint8_t
    {
        STRING,
        INSTANCE,
        VECTOR,
    };

    Type type;
    bool marked = false;
    Obj* next = nullptr; // intrusive list of all objects, for sweeping

    explicit Obj(Type type) : type(type)
    {
    }
    virtual ~Obj() = default;
};

struct ObjString : Obj
{
    std::string value;
    explicit ObjString(std::string value) : Obj(Type::STRING), value(std::move(value))
    {
    }
};

struct ObjInstance : Obj
{
    const ClassInfo* klass;
    std::vector<Value> fields;
    explicit ObjInstance(const ClassInfo* klass)
        : Obj(Type::INSTANCE), klass(klass),
          fields(klass->field_kinds.size(), int_value(0))
    {
    }
};

// A 'vector<T>'. The element kind is all that is left of T at runtime: whether the
// elements are references (for the GC) and how to print them.
struct ObjVector : Obj
{
    ValueKind element_kind;
    std::vector<Value> items;
    explicit ObjVector(ValueKind element_kind)
        : Obj(Type::VECTOR), element_kind(element_kind)
    {
    }
};
