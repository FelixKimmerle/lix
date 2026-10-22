#include "Type.hpp"

#include "ast/Stmt.hpp"

Type::Type(TypeKind kind, std::string name) : kind(kind), name(std::move(name))
{
}

ValueKind Type::value_kind() const
{
    switch (kind)
    {
    case TypeKind::INT:
        return ValueKind::INT;
    case TypeKind::FLOAT:
        return ValueKind::FLOAT;
    case TypeKind::BOOL:
        return ValueKind::BOOL;
    default:
        return ValueKind::REF;
    }
}

bool Type::is_numeric() const
{
    return kind == TypeKind::INT || kind == TypeKind::FLOAT;
}

bool Type::is_reference() const
{
    return kind == TypeKind::STRING || kind == TypeKind::VECTOR ||
           kind == TypeKind::CLASS || kind == TypeKind::NULL_T;
}

VectorType::VectorType(const Type* element)
    : Type(TypeKind::VECTOR, "vector<" + element->name + ">"), element(element)
{
}

namespace
{
    std::string class_name(ClassDecl* decl, const std::vector<const Type*>& args)
    {
        std::string result = decl->name;
        if (!args.empty())
        {
            result += "<";
            for (size_t i = 0; i < args.size(); i++)
            {
                if (i > 0)
                {
                    result += ", ";
                }
                result += args[i]->name;
            }
            result += ">";
        }
        return result;
    }
} // namespace

ClassType::ClassType(ClassDecl* decl, std::vector<const Type*> type_args,
                     std::string name)
    : Type(TypeKind::CLASS, std::move(name)), decl(decl), type_args(std::move(type_args))
{
}

int ClassType::find_field(const std::string& field_name) const
{
    for (size_t i = 0; i < fields.size(); i++)
    {
        if (fields[i].name == field_name)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

TypeTable::TypeTable()
{
    error_t = add(std::make_unique<Type>(TypeKind::ERROR, "<error>"));
    void_t = add(std::make_unique<Type>(TypeKind::VOID, "void"));
    null_t = add(std::make_unique<Type>(TypeKind::NULL_T, "null"));
    bool_t = add(std::make_unique<Type>(TypeKind::BOOL, "bool"));
    int_t = add(std::make_unique<Type>(TypeKind::INT, "int"));
    float_t = add(std::make_unique<Type>(TypeKind::FLOAT, "float"));
    string_t = add(std::make_unique<Type>(TypeKind::STRING, "string"));
}

const Type* TypeTable::add(std::unique_ptr<Type> type)
{
    storage.push_back(std::move(type));
    return storage.back().get();
}

const Type* TypeTable::error_type() const
{
    return error_t;
}
const Type* TypeTable::void_type() const
{
    return void_t;
}
const Type* TypeTable::null_type() const
{
    return null_t;
}
const Type* TypeTable::bool_type() const
{
    return bool_t;
}
const Type* TypeTable::int_type() const
{
    return int_t;
}
const Type* TypeTable::float_type() const
{
    return float_t;
}
const Type* TypeTable::string_type() const
{
    return string_t;
}

const VectorType* TypeTable::vector_of(const Type* element)
{
    auto found = vectors.find(element);
    if (found != vectors.end())
    {
        return found->second;
    }
    auto vector = std::make_unique<VectorType>(element);
    const VectorType* result = vector.get();
    storage.push_back(std::move(vector));
    vectors[element] = result;
    return result;
}

ClassType* TypeTable::find_class(ClassDecl* decl,
                                 const std::vector<const Type*>& args) const
{
    auto found = classes.find({decl, args});
    return found == classes.end() ? nullptr : found->second;
}

ClassType* TypeTable::create_class(ClassDecl* decl, const std::vector<const Type*>& args)
{
    auto type = std::make_unique<ClassType>(decl, args, class_name(decl, args));
    ClassType* result = type.get();
    storage.push_back(std::move(type));
    classes[{decl, args}] = result;
    class_order.push_back({decl, args});
    return result;
}

size_t TypeTable::class_count() const
{
    return class_order.size();
}

void TypeTable::forget_classes(size_t count)
{
    for (size_t i = count; i < class_order.size(); i++)
    {
        classes.erase(class_order[i]);
    }
    class_order.resize(count);
}
