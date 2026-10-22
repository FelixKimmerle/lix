#pragma once

#include "bytecode/Value.hpp"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct ClassDecl;
struct FuncDecl;
struct VarStmt;
struct FunctionInfo;

enum class TypeKind
{
    ERROR, // result of an expression that already produced an error; silences follow-ups
    VOID,
    NULL_T, // type of the 'null' literal, convertible to any class type
    BOOL,
    INT,
    FLOAT,
    STRING,
    VECTOR,
    CLASS,
};

// A resolved, semantic type.
//
// This replaces the old 'enum LixDatatype'. An enum can only describe a fixed set of
// types; composite types like 'vector<vector<Point>>' need a recursive structure. Types
// are interned by the TypeTable, so there is exactly one 'vector<int>' object and two
// types are equal if and only if their pointers are equal.
struct Type
{
    TypeKind kind;
    std::string name;

    Type(TypeKind kind, std::string name);
    virtual ~Type() = default;

    // How a value of this type is stored in a VM slot. Strings, vectors and class
    // instances are references to heap objects.
    ValueKind value_kind() const;
    bool is_numeric() const;
    bool is_reference() const;
};

struct VectorType : Type
{
    const Type* element;
    explicit VectorType(const Type* element);
};

using TypeSubstitution = std::unordered_map<std::string, const Type*>;

struct Field
{
    std::string name;
    const Type* type;
    VarStmt* decl;
};

// A concrete class. For templates there is one ClassType per instantiation: 'Box<int>'
// and 'Box<string>' are two different ClassTypes that share the same ClassDecl. Their
// methods are compiled separately (monomorphization), just like C++ templates, because
// the bytecode is typed: 'Box<int>::get' adds with ADD_INT and 'Box<float>::get' with
// ADD_FLOAT.
struct ClassType : Type
{
    ClassDecl* decl;
    std::vector<const Type*> type_args;
    TypeSubstitution substitution; // template parameter name -> type argument

    std::vector<Field> fields;
    std::unordered_map<std::string, FunctionInfo*> methods;
    FunctionInfo* constructor = nullptr;
    uint16_t class_index = 0; // index into Program::classes

    ClassType(ClassDecl* decl, std::vector<const Type*> type_args, std::string name);
    int find_field(const std::string& field_name) const;
};

// Compile time information about a callable function, method or constructor.
// For methods and constructors params[0] is the receiver ('this').
struct FunctionInfo
{
    std::string name;
    std::vector<const Type*> params;
    const Type* return_type;
    uint16_t index; // index into Program::functions
    ClassType* owner;
    FuncDecl* decl; // null for synthesized default constructors
};

// Owns and interns all types.
class TypeTable
{
  private:
    std::vector<std::unique_ptr<Type>> storage;
    std::unordered_map<const Type*, const VectorType*> vectors;
    std::map<std::pair<ClassDecl*, std::vector<const Type*>>, ClassType*> classes;
    std::vector<std::pair<ClassDecl*, std::vector<const Type*>>> class_order;

    const Type* error_t;
    const Type* void_t;
    const Type* null_t;
    const Type* bool_t;
    const Type* int_t;
    const Type* float_t;
    const Type* string_t;

    const Type* add(std::unique_ptr<Type> type);

  public:
    TypeTable();

    const Type* error_type() const;
    const Type* void_type() const;
    const Type* null_type() const;
    const Type* bool_type() const;
    const Type* int_type() const;
    const Type* float_type() const;
    const Type* string_type() const;

    const VectorType* vector_of(const Type* element);

    ClassType* find_class(ClassDecl* decl, const std::vector<const Type*>& args) const;
    ClassType* create_class(ClassDecl* decl, const std::vector<const Type*>& args);

    // For rolling back a failed REPL entry: classes created after the first 'count'
    // are no longer found. They stay allocated, so no dangling pointers remain.
    size_t class_count() const;
    void forget_classes(size_t count);
};
