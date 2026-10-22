#pragma once

#include <cstdint>

// Operand formats, used by the disassembler.
enum class OperandFormat : uint8_t
{
    NONE,
    BYTE,     // u8
    LOCAL,    // u8 slot relative to the frame base
    CONSTANT, // u16 index into Chunk::constants
    STRING,   // u16 index into Program::strings
    GLOBAL,   // u16 index into the globals
    FIELD,    // u16 field index
    FUNCTION, // u16 index into Program::functions
    CLASS,    // u16 index into Program::classes
    KIND,     // u8 ValueKind
    JUMP,     // u16 forward offset
    LOOP,     // u16 backward offset
};

// Stack effects are written as [before] -> [after], top of the stack on the right.
//
// The instruction set is statically typed, like the JVM's: there is an ADD_INT and an
// ADD_FLOAT instead of one ADD that inspects its operands at runtime. The compiler picks
// the right instruction from the static types, so the VM never checks types.
#define LIX_OPCODES(X)                                                                   \
    /* constants */                                                                      \
    X(CONST, CONSTANT)      /* [] -> [value] */                                          \
    X(CONST_STRING, STRING) /* [] -> [string] */                                         \
    X(TRUE, NONE)                                                                        \
    X(FALSE, NONE)                                                                       \
    X(NULL_REF, NONE)                                                                    \
    /* stack manipulation */                                                             \
    X(POP, NONE)                                                                         \
    X(POPN, BYTE)                                                                        \
    X(DUP, NONE)  /* [a] -> [a a] */                                                     \
    X(DUP2, NONE) /* [a b] -> [a b a b] */                                               \
    /* variables */                                                                      \
    X(GET_LOCAL, LOCAL)                                                                  \
    X(SET_LOCAL, LOCAL) /* [v] -> [v] */                                                 \
    X(GET_GLOBAL, GLOBAL)                                                                \
    X(SET_GLOBAL, GLOBAL) /* [v] -> [v] */                                               \
    X(GET_FIELD, FIELD)   /* [obj] -> [v] */                                             \
    X(SET_FIELD, FIELD)   /* [obj v] -> [v] */                                           \
    /* integer arithmetic */                                                             \
    X(ADD_INT, NONE)                                                                     \
    X(SUB_INT, NONE)                                                                     \
    X(MUL_INT, NONE)                                                                     \
    X(DIV_INT, NONE)                                                                     \
    X(MOD_INT, NONE)                                                                     \
    X(NEG_INT, NONE)                                                                     \
    /* float arithmetic */                                                               \
    X(ADD_FLOAT, NONE)                                                                   \
    X(SUB_FLOAT, NONE)                                                                   \
    X(MUL_FLOAT, NONE)                                                                   \
    X(DIV_FLOAT, NONE)                                                                   \
    X(NEG_FLOAT, NONE)                                                                   \
    X(CONCAT, NONE) /* [string string] -> [string] */                                    \
    X(NOT, NONE)                                                                         \
    /* comparison */                                                                     \
    X(EQ_INT, NONE)                                                                      \
    X(LT_INT, NONE)                                                                      \
    X(LE_INT, NONE)                                                                      \
    X(GT_INT, NONE)                                                                      \
    X(GE_INT, NONE)                                                                      \
    X(EQ_FLOAT, NONE)                                                                    \
    X(LT_FLOAT, NONE)                                                                    \
    X(LE_FLOAT, NONE)                                                                    \
    X(GT_FLOAT, NONE)                                                                    \
    X(GE_FLOAT, NONE)                                                                    \
    X(EQ_BOOL, NONE)                                                                     \
    X(EQ_STRING, NONE) /* compares contents */                                           \
    X(LT_STRING, NONE) /* lexicographic */                                               \
    X(LE_STRING, NONE)                                                                   \
    X(GT_STRING, NONE)                                                                   \
    X(GE_STRING, NONE)                                                                   \
    X(EQ_REF, NONE) /* compares identity */                                              \
    /* conversions */                                                                    \
    X(INT_TO_FLOAT, NONE)                                                                \
    X(INT_TO_FLOAT_UNDER, NONE) /* [int x] -> [float x]: converts the second slot */     \
    X(FLOAT_TO_INT, NONE)                                                                \
    X(INT_TO_BOOL, NONE)                                                                 \
    X(BOOL_TO_INT, NONE)                                                                 \
    X(INT_TO_STRING, NONE)                                                               \
    X(FLOAT_TO_STRING, NONE)                                                             \
    X(BOOL_TO_STRING, NONE)                                                              \
    /* control flow */                                                                   \
    X(JUMP, JUMP)                                                                        \
    X(JUMP_IF_FALSE, JUMP)      /* pops the condition */                                 \
    X(JUMP_IF_FALSE_KEEP, JUMP) /* leaves the condition, for && */                       \
    X(JUMP_IF_TRUE_KEEP, JUMP)  /* leaves the condition, for || */                       \
    X(LOOP, LOOP)                                                                        \
    X(CALL, FUNCTION) /* [args...] -> [result] (nothing for void) */                     \
    X(RETURN, NONE)                                                                      \
    X(RETURN_VOID, NONE)                                                                 \
    /* objects */                                                                        \
    X(NEW, CLASS)     /* [] -> [instance], all fields zero/null */                       \
    X(VEC_NEW, KIND)  /* [] -> [vector] */                                               \
    X(VEC_PUSH, NONE) /* [vector v] -> [] */                                             \
    X(VEC_POP, NONE)  /* [vector] -> [v] */                                              \
    X(VEC_GET, NONE)  /* [vector index] -> [v] */                                        \
    X(VEC_SET, NONE)  /* [vector index v] -> [v] */                                      \
    X(VEC_LEN, NONE)                                                                     \
    X(VEC_CLEAR, NONE) /* [vector] -> [] */                                              \
    X(STR_LEN, NONE)                                                                     \
    /* output */                                                                         \
    X(PRINT_INT, NONE)                                                                   \
    X(PRINT_FLOAT, NONE)                                                                 \
    X(PRINT_BOOL, NONE)                                                                  \
    X(PRINT_REF, NONE)

enum class OpCode : uint8_t
{
#define LIX_OPCODE_ENUM(name, format) name,
    LIX_OPCODES(LIX_OPCODE_ENUM)
#undef LIX_OPCODE_ENUM
};

const char* opcode_name(OpCode op);
OperandFormat opcode_format(OpCode op);
