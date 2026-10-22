#pragma once

#include <cstdint>

struct Obj;

// How a value is stored in a slot. This is all the VM knows about types: the compiler
// has already checked everything, so the VM only needs to know which slots hold heap
// references (for the garbage collector) and how to print values inside containers.
enum class ValueKind : uint8_t
{
    INT,
    FLOAT,
    BOOL,
    REF,
};

// Every value occupies exactly one 8 byte slot, whatever its type. The old Lix VM used a
// byte stack where an int took 4 bytes and a bool 1, so every instruction had to know
// the size of every type. With a uniform slot a 'vector<Point>' is just as easy to
// store, copy and pass around as an 'int': it is a single pointer.
union Value {
    int64_t i;
    double f;
    bool b;
    Obj* o;
};

static_assert(sizeof(Value) == 8);

inline Value int_value(int64_t i)
{
    Value v;
    v.i = i;
    return v;
}

inline Value float_value(double f)
{
    Value v;
    v.f = f;
    return v;
}

inline Value bool_value(bool b)
{
    Value v;
    v.i = 0;
    v.b = b;
    return v;
}

inline Value ref_value(Obj* o)
{
    Value v;
    v.o = o;
    return v;
}
