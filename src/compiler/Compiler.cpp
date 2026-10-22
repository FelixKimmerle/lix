#include "Compiler.hpp"

#include <cassert>
#include <limits>

namespace
{
    // Re-enables error reporting while a template is instantiated. An instantiation is
    // cached, so errors inside it have to be reported the first time even if that
    // happens while the compiler is only probing the type of an expression.
    class UnmuteGuard
    {
      private:
        Diagnostics& diagnostics;
        int saved;

      public:
        explicit UnmuteGuard(Diagnostics& diagnostics)
            : diagnostics(diagnostics), saved(diagnostics.mute_level())
        {
            diagnostics.set_mute_level(0);
        }
        ~UnmuteGuard()
        {
            diagnostics.set_mute_level(saved);
        }
    };

    std::string quote(const std::string& name)
    {
        return "'" + name + "'";
    }

    constexpr int MAX_INSTANTIATION_DEPTH = 64;
    constexpr size_t MAX_INSTANTIATIONS = 4096;
    constexpr size_t MAX_LOCALS = 256;
} // namespace

Compiler::Compiler(Diagnostics& diagnostics)
    : diagnostics(&diagnostics), program(std::make_unique<Program>())
{
}

void Compiler::set_diagnostics(Diagnostics& new_diagnostics)
{
    diagnostics = &new_diagnostics;
}

void Compiler::set_repl_mode(bool enabled)
{
    repl_mode = enabled;
}

Program& Compiler::get_program()
{
    return *program;
}

// ==================================================================================
// Entry point
// ==================================================================================

std::optional<uint16_t> Compiler::compile(std::vector<StmtPtr>& module)
{
    Checkpoint checkpoint = make_checkpoint();
    new_declarations.clear();
    collect_declarations(module);

    FunctionInfo* script_info = new_function(repl_mode ? "<repl>" : "<script>", {},
                                             types.void_type(), nullptr, nullptr);
    program->script = script_info->index;

    // Instantiate non-template classes and declare non-template functions up front, so
    // that everything can be used before it is declared.
    for (auto& stmt : module)
    {
        if (auto* decl = dynamic_cast<ClassDecl*>(stmt.get());
            decl && decl->type_params.empty())
        {
            instantiate_class(decl, {}, decl->name_sector);
        }
    }
    for (auto& stmt : module)
    {
        if (auto* decl = dynamic_cast<FuncDecl*>(stmt.get());
            decl && decl->type_params.empty())
        {
            instantiate_function(decl, {}, decl->name_sector);
        }
    }

    FunctionCtx script;
    script.info = script_info;
    script.function = program->functions[script_info->index].get();
    script.is_script = true;
    ctx = &script;

    // Global variables are declared up front too, so functions can use them. Their
    // default values are stored first, so a 'vector<int>' global is an empty vector
    // rather than null even if a function reads it before its declaration has run.
    std::vector<std::string> predeclared;
    for (auto& stmt : module)
    {
        auto* var = dynamic_cast<VarStmt*>(stmt.get());
        if (!var || var->type->name == "auto")
        {
            continue;
        }
        line = var->sector.begin.line + 1;
        if (globals.count(var->name))
        {
            diagnostics->error(var->sector, "Redefinition of global variable " +
                                                quote(var->name) + ".");
            continue;
        }
        const Type* type = resolve_value_type(*var->type, nullptr, nullptr,
                                              "global " + quote(var->name));
        uint16_t index = static_cast<uint16_t>(program->global_names.size());
        program->global_names.push_back(var->name);
        program->global_kinds.push_back(type->value_kind());
        globals[var->name] = Global{type, index, false};
        predeclared.push_back(var->name);
    }
    for (const std::string& name : predeclared)
    {
        const Global& global = globals[name];
        emit_default(global.type);
        emit(OpCode::SET_GLOBAL);
        emit_u16(global.index);
        emit(OpCode::POP);
    }

    for (auto& stmt : module)
    {
        if (dynamic_cast<FuncDecl*>(stmt.get()) || dynamic_cast<ClassDecl*>(stmt.get()))
        {
            continue;
        }
        compile_stmt(stmt.get());
    }
    emit(OpCode::RETURN_VOID);

    while (!pending.empty())
    {
        PendingBody body = std::move(pending.front());
        pending.pop_front();
        compile_body(body);
    }
    ctx = nullptr;

    if (diagnostics->had_error())
    {
        rollback(checkpoint);
        return std::nullopt;
    }
    return script_info->index;
}

Compiler::Checkpoint Compiler::make_checkpoint() const
{
    return Checkpoint{program->functions.size(), program->classes.size(),
                      program->global_names.size(), types.class_count(), lazy_bodies};
}

// Undoes everything a failed compile() declared, so that a REPL entry with an error
// leaves no half-compiled functions or classes behind that later entries could call.
void Compiler::rollback(Checkpoint& checkpoint)
{
    for (const std::string& name : new_declarations)
    {
        class_decls.erase(name);
        function_decls.erase(name);
    }
    std::erase_if(function_instances, [&](const auto& entry) {
        return entry.second->index >= checkpoint.functions;
    });
    std::erase_if(globals, [&](const auto& entry) {
        return entry.second.index >= checkpoint.globals;
    });
    function_infos.resize(checkpoint.functions);
    program->functions.resize(checkpoint.functions);
    program->classes.resize(checkpoint.classes);
    program->global_names.resize(checkpoint.globals);
    program->global_kinds.resize(checkpoint.globals);
    types.forget_classes(checkpoint.type_classes);
    pending.clear();

    // Template members that were still lazy at the checkpoint may have been compiled
    // since. Make them lazy again, with empty code.
    lazy_bodies = std::move(checkpoint.lazy_bodies);
    for (auto& [info, body] : lazy_bodies)
    {
        program->functions[info->index]->chunk = Chunk();
    }
}

void Compiler::collect_declarations(std::vector<StmtPtr>& module)
{
    for (auto& stmt : module)
    {
        if (auto* decl = dynamic_cast<ClassDecl*>(stmt.get()))
        {
            if (decl->name == "vector")
            {
                diagnostics->error(
                    decl->name_sector,
                    "'vector' is a built-in type and cannot be redefined.");
            }
            else if (class_decls.count(decl->name) || function_decls.count(decl->name))
            {
                diagnostics->error(decl->name_sector,
                                   "Redefinition of " + quote(decl->name) + ".");
            }
            else
            {
                class_decls[decl->name] = decl;
                new_declarations.push_back(decl->name);
            }
        }
        else if (auto* func = dynamic_cast<FuncDecl*>(stmt.get()))
        {
            if (class_decls.count(func->name) || function_decls.count(func->name))
            {
                diagnostics->error(func->name_sector,
                                   "Redefinition of " + quote(func->name) +
                                       " (function overloading is not supported).");
            }
            else
            {
                function_decls[func->name] = func;
                new_declarations.push_back(func->name);
            }
        }
    }
}

// ==================================================================================
// Types and templates
// ==================================================================================

const Type* Compiler::resolve_type(const TypeExpr& type_expr)
{
    return resolve_type(type_expr, ctx ? ctx->substitution : nullptr,
                        ctx ? ctx->owner : nullptr);
}

const Type* Compiler::resolve_type(const TypeExpr& type_expr,
                                   const TypeSubstitution* substitution, ClassType* owner)
{
    const std::string& name = type_expr.name;
    auto expect_no_args = [&]() {
        if (!type_expr.args.empty())
        {
            diagnostics->error(type_expr.sector,
                               quote(name) + " does not take template arguments.");
        }
    };

    if (name == "int" || name == "float" || name == "bool" || name == "string" ||
        name == "void")
    {
        expect_no_args();
        if (name == "int")
        {
            return types.int_type();
        }
        if (name == "float")
        {
            return types.float_type();
        }
        if (name == "bool")
        {
            return types.bool_type();
        }
        if (name == "string")
        {
            return types.string_type();
        }
        return types.void_type();
    }
    if (name == "auto")
    {
        diagnostics->error(type_expr.sector,
                           "'auto' is only allowed for variables with an initializer.");
        return types.error_type();
    }
    if (name == "vector")
    {
        if (type_expr.args.size() != 1)
        {
            diagnostics->error(
                type_expr.sector,
                "'vector' needs exactly one type argument, like vector<int>.");
            return types.error_type();
        }
        const Type* element = resolve_value_type(*type_expr.args[0], substitution, owner,
                                                 "a vector element");
        if (element->kind == TypeKind::ERROR)
        {
            return element;
        }
        return types.vector_of(element);
    }
    if (substitution)
    {
        auto found = substitution->find(name);
        if (found != substitution->end())
        {
            expect_no_args();
            return found->second;
        }
    }

    auto decl_it = class_decls.find(name);
    if (decl_it == class_decls.end())
    {
        diagnostics->error(type_expr.sector, "Unknown type " + quote(name) + ".");
        return types.error_type();
    }
    ClassDecl* decl = decl_it->second;

    if (decl->type_params.empty())
    {
        expect_no_args();
        ClassType* type = instantiate_class(decl, {}, type_expr.sector);
        return type ? type : types.error_type();
    }
    // Inside a template class, its plain name refers to the current instantiation,
    // like the injected class name in C++: inside 'Node<T>', 'Node' means 'Node<T>'.
    if (type_expr.args.empty() && owner && owner->decl == decl)
    {
        return owner;
    }
    if (type_expr.args.size() != decl->type_params.size())
    {
        diagnostics->error(type_expr.sector,
                           "Template " + quote(name) + " expects " +
                               std::to_string(decl->type_params.size()) +
                               " type argument(s), got " +
                               std::to_string(type_expr.args.size()) + ".");
        return types.error_type();
    }
    std::vector<const Type*> args;
    for (auto& arg : type_expr.args)
    {
        const Type* type =
            resolve_value_type(*arg, substitution, owner, "a template argument");
        if (type->kind == TypeKind::ERROR)
        {
            return type;
        }
        args.push_back(type);
    }
    ClassType* type = instantiate_class(decl, args, type_expr.sector);
    return type ? type : types.error_type();
}

const Type* Compiler::resolve_value_type(const TypeExpr& type_expr,
                                         const TypeSubstitution* substitution,
                                         ClassType* owner, const std::string& what)
{
    const Type* type = resolve_type(type_expr, substitution, owner);
    if (type->kind == TypeKind::VOID)
    {
        diagnostics->error(type_expr.sector, what + " cannot have type 'void'.");
        return types.error_type();
    }
    return type;
}

FunctionInfo* Compiler::new_function(const std::string& name,
                                     std::vector<const Type*> params,
                                     const Type* return_type, ClassType* owner,
                                     FuncDecl* decl)
{
    auto function = std::make_unique<Function>();
    function->name = name;
    function->arity = static_cast<uint16_t>(params.size());
    function->is_method = owner != nullptr;

    auto info = std::make_unique<FunctionInfo>();
    info->name = name;
    info->params = std::move(params);
    info->return_type = return_type;
    info->index = static_cast<uint16_t>(program->functions.size());
    info->owner = owner;
    info->decl = decl;

    if (program->functions.size() >= std::numeric_limits<uint16_t>::max())
    {
        diagnostics->error(decl ? decl->name_sector : SourceSector(),
                           "Too many functions.");
    }
    program->functions.push_back(std::move(function));
    function_infos.push_back(std::move(info));
    return function_infos.back().get();
}

ClassType* Compiler::instantiate_class(ClassDecl* decl,
                                       const std::vector<const Type*>& args,
                                       const SourceSector& where)
{
    if (ClassType* existing = types.find_class(decl, args))
    {
        return existing;
    }
    UnmuteGuard unmute(*diagnostics);

    if (args.size() != decl->type_params.size())
    {
        diagnostics->error(where, "Template " + quote(decl->name) + " expects " +
                                      std::to_string(decl->type_params.size()) +
                                      " type argument(s).");
        return nullptr;
    }
    if (instantiation_depth >= MAX_INSTANTIATION_DEPTH ||
        program->classes.size() >= MAX_INSTANTIATIONS)
    {
        diagnostics->error(
            where, "Too many nested template instantiations while instantiating " +
                       quote(decl->name) + " (infinitely recursive template?).");
        return nullptr;
    }

    // Register the class before resolving its members, so that self-referencing types
    // like 'class Node { Node next; }' find the (still incomplete) class in the cache.
    ClassType* type = types.create_class(decl, args);
    for (size_t i = 0; i < args.size(); i++)
    {
        type->substitution[decl->type_params[i]] = args[i];
    }
    type->class_index = static_cast<uint16_t>(program->classes.size());
    program->classes.push_back(
        std::make_unique<ClassInfo>(ClassInfo{type->name, {}, {}}));

    instantiation_depth++;
    for (auto& field : decl->fields)
    {
        if (type->find_field(field->name) >= 0)
        {
            diagnostics->error(field->sector,
                               "Duplicate field " + quote(field->name) + ".");
            continue;
        }
        const Type* field_type =
            field->type->name == "auto"
                ? (diagnostics->error(field->type->sector,
                                      "Fields cannot be declared 'auto'."),
                   types.error_type())
                : resolve_value_type(*field->type, &type->substitution, type,
                                     "field " + quote(field->name));
        type->fields.push_back(Field{field->name, field_type, field.get()});
        program->classes[type->class_index]->field_names.push_back(field->name);
        program->classes[type->class_index]->field_kinds.push_back(
            field_type->value_kind());
    }

    for (auto& method : decl->methods)
    {
        if (type->methods.count(method->name) || type->find_field(method->name) >= 0)
        {
            diagnostics->error(method->name_sector,
                               "Duplicate member " + quote(method->name) + ".");
            continue;
        }
        std::vector<const Type*> params = {type};
        for (auto& param : method->params)
        {
            params.push_back(resolve_value_type(*param.type, &type->substitution, type,
                                                "parameter " + quote(param.name)));
        }
        const Type* return_type =
            resolve_type(*method->return_type, &type->substitution, type);
        FunctionInfo* info =
            new_function(type->name + "::" + method->name, std::move(params), return_type,
                         type, method.get());
        type->methods[method->name] = info;
        queue_body(PendingBody{info, method.get(), type, type->substitution});
    }

    // Every class has exactly one constructor. Without a user-written one, a constructor
    // without parameters is synthesized that only runs the field initializers.
    FuncDecl* ctor = decl->constructor.get();
    std::vector<const Type*> ctor_params = {type};
    if (ctor)
    {
        for (auto& param : ctor->params)
        {
            ctor_params.push_back(resolve_value_type(*param.type, &type->substitution,
                                                     type,
                                                     "parameter " + quote(param.name)));
        }
    }
    type->constructor = new_function(type->name + "::" + decl->name,
                                     std::move(ctor_params), type, type, ctor);
    queue_body(PendingBody{type->constructor, ctor, type, type->substitution});

    instantiation_depth--;
    return type;
}

FunctionInfo* Compiler::instantiate_function(FuncDecl* decl,
                                             const std::vector<const Type*>& args,
                                             const SourceSector& where)
{
    auto key = std::make_pair(decl, args);
    auto found = function_instances.find(key);
    if (found != function_instances.end())
    {
        return found->second;
    }
    UnmuteGuard unmute(*diagnostics);

    if (function_instances.size() >= MAX_INSTANTIATIONS)
    {
        diagnostics->error(where,
                           "Too many template instantiations while instantiating " +
                               quote(decl->name) + " (infinitely recursive template?).");
        return nullptr;
    }

    TypeSubstitution substitution;
    std::string name = decl->name;
    if (!args.empty())
    {
        name += "<";
        for (size_t i = 0; i < args.size(); i++)
        {
            substitution[decl->type_params[i]] = args[i];
            name += (i > 0 ? ", " : "") + args[i]->name;
        }
        name += ">";
    }

    std::vector<const Type*> params;
    for (auto& param : decl->params)
    {
        params.push_back(resolve_value_type(*param.type, &substitution, nullptr,
                                            "parameter " + quote(param.name)));
    }
    const Type* return_type = resolve_type(*decl->return_type, &substitution, nullptr);

    FunctionInfo* info =
        new_function(name, std::move(params), return_type, nullptr, decl);
    function_instances[key] = info;
    pending.push_back(PendingBody{info, decl, nullptr, std::move(substitution)});
    return info;
}

void Compiler::queue_body(PendingBody body)
{
    if (body.owner && !body.owner->decl->type_params.empty())
    {
        FunctionInfo* info = body.info;
        lazy_bodies.emplace(info, std::move(body));
    }
    else
    {
        pending.push_back(std::move(body));
    }
}

void Compiler::compile_body(PendingBody& body)
{
    FunctionCtx function_ctx;
    function_ctx.info = body.info;
    function_ctx.function = program->functions[body.info->index].get();
    function_ctx.owner = body.owner;
    function_ctx.substitution = &body.substitution;
    function_ctx.is_constructor = body.owner && body.info == body.owner->constructor;

    FunctionCtx* saved_ctx = ctx;
    ctx = &function_ctx;
    line = body.decl ? body.decl->sector.begin.line + 1
                     : body.owner->decl->sector.begin.line + 1;

    size_t first_param = 0;
    if (body.owner)
    {
        ctx->locals.push_back(Local{"this", body.owner, 0});
        first_param = 1;
    }
    if (body.decl)
    {
        for (size_t i = 0; i < body.decl->params.size(); i++)
        {
            const Param& param = body.decl->params[i];
            declare_local(param.name, body.info->params[first_param + i], param.sector);
        }
    }

    if (ctx->is_constructor)
    {
        // Field initializers run at the start of the constructor.
        for (size_t i = 0; i < body.owner->fields.size(); i++)
        {
            const Field& field = body.owner->fields[i];
            emit(OpCode::GET_LOCAL);
            emit_byte(0);
            if (field.decl->initializer)
            {
                const Type* type =
                    compile_expr(field.decl->initializer.get(), field.type);
                coerce(type, field.type, field.decl->initializer->sector,
                       "initializer of field " + quote(field.name));
            }
            else
            {
                emit_default(field.type);
            }
            emit(OpCode::SET_FIELD);
            emit_u16(static_cast<uint16_t>(i));
            emit(OpCode::POP);
        }
    }

    if (body.decl)
    {
        compile_stmt(body.decl->body.get());
    }

    if (ctx->is_constructor)
    {
        emit(OpCode::GET_LOCAL);
        emit_byte(0);
        emit(OpCode::RETURN);
    }
    else if (body.info->return_type->kind == TypeKind::VOID ||
             body.info->return_type->kind == TypeKind::ERROR)
    {
        emit(OpCode::RETURN_VOID);
    }
    else if (!always_returns(body.decl->body.get()))
    {
        diagnostics->error(body.decl->name_sector, "Not all paths of " +
                                                       quote(body.info->name) +
                                                       " return a value.");
    }

    ctx = saved_ctx;
}

// ==================================================================================
// Emitting code
// ==================================================================================

Chunk& Compiler::chunk()
{
    return ctx->function->chunk;
}

void Compiler::emit(OpCode op)
{
    chunk().write(op, line);
}

void Compiler::emit_byte(uint8_t byte)
{
    chunk().write(byte, line);
}

void Compiler::emit_u16(uint16_t value)
{
    chunk().write_u16(value, line);
}

void Compiler::emit_constant(Value value, ValueKind kind)
{
    if (chunk().constants.size() >= std::numeric_limits<uint16_t>::max())
    {
        diagnostics->error(SourceSector(), "Too many constants in one function.");
        return;
    }
    chunk().constants.push_back(value);
    chunk().constant_kinds.push_back(kind);
    emit(OpCode::CONST);
    emit_u16(static_cast<uint16_t>(chunk().constants.size() - 1));
}

void Compiler::emit_string(const std::string& value)
{
    auto found = string_constants.find(value);
    uint16_t index;
    if (found != string_constants.end())
    {
        index = found->second;
    }
    else
    {
        index = static_cast<uint16_t>(program->strings.size());
        program->strings.push_back(value);
        string_constants[value] = index;
    }
    emit(OpCode::CONST_STRING);
    emit_u16(index);
}

size_t Compiler::emit_jump(OpCode op)
{
    emit(op);
    emit_u16(0xffff);
    return chunk().code.size() - 2;
}

void Compiler::patch_jump(size_t operand_offset)
{
    size_t jump = chunk().code.size() - operand_offset - 2;
    if (jump > std::numeric_limits<uint16_t>::max())
    {
        diagnostics->error(SourceSector(), "Too much code to jump over.");
    }
    chunk().code[operand_offset] = static_cast<uint8_t>(jump & 0xff);
    chunk().code[operand_offset + 1] = static_cast<uint8_t>((jump >> 8) & 0xff);
}

void Compiler::emit_loop(size_t loop_start)
{
    emit(OpCode::LOOP);
    size_t offset = chunk().code.size() - loop_start + 2;
    if (offset > std::numeric_limits<uint16_t>::max())
    {
        diagnostics->error(SourceSector(), "Loop body too large.");
    }
    emit_u16(static_cast<uint16_t>(offset));
}

// Default values: 0, 0.0, false, "", an empty vector, or null for class types.
void Compiler::emit_default(const Type* type)
{
    switch (type->kind)
    {
    case TypeKind::INT:
        emit_constant(int_value(0), ValueKind::INT);
        break;
    case TypeKind::FLOAT:
        emit_constant(float_value(0.0), ValueKind::FLOAT);
        break;
    case TypeKind::BOOL:
        emit(OpCode::FALSE);
        break;
    case TypeKind::STRING:
        emit_string("");
        break;
    case TypeKind::VECTOR:
        emit(OpCode::VEC_NEW);
        emit_byte(static_cast<uint8_t>(
            static_cast<const VectorType*>(type)->element->value_kind()));
        break;
    default:
        emit(OpCode::NULL_REF);
        break;
    }
}

void Compiler::emit_pops(int count)
{
    if (count == 1)
    {
        emit(OpCode::POP);
    }
    else if (count > 1)
    {
        emit(OpCode::POPN);
        emit_byte(static_cast<uint8_t>(count));
    }
}

// ==================================================================================
// Scopes and locals
// ==================================================================================

void Compiler::begin_scope()
{
    ctx->scope_depth++;
}

void Compiler::end_scope()
{
    ctx->scope_depth--;
    int count = 0;
    while (!ctx->locals.empty() && ctx->locals.back().depth > ctx->scope_depth)
    {
        ctx->locals.pop_back();
        count++;
    }
    emit_pops(count);
}

void Compiler::declare_local(const std::string& name, const Type* type,
                             const SourceSector& sector)
{
    for (auto local = ctx->locals.rbegin(); local != ctx->locals.rend(); ++local)
    {
        if (local->depth < ctx->scope_depth)
        {
            break;
        }
        if (local->name == name)
        {
            diagnostics->error(sector,
                               "Redefinition of " + quote(name) + " in the same scope.");
            break;
        }
    }
    if (ctx->locals.size() >= MAX_LOCALS)
    {
        diagnostics->error(sector, "Too many local variables in one function.");
    }
    ctx->locals.push_back(Local{name, type, ctx->scope_depth});
}

int Compiler::resolve_local(const std::string& name) const
{
    for (int i = static_cast<int>(ctx->locals.size()) - 1; i >= 0; i--)
    {
        if (ctx->locals[i].name == name)
        {
            return i;
        }
    }
    return -1;
}

int Compiler::locals_above(int depth) const
{
    int count = 0;
    for (const Local& local : ctx->locals)
    {
        if (local.depth > depth)
        {
            count++;
        }
    }
    return count;
}

// ==================================================================================
// Expressions: helpers
// ==================================================================================

const Type* Compiler::compile_expr(Expr* expr, const Type* expected)
{
    const Type* saved_expected = expected_type;
    uint32_t saved_line = line;
    expected_type = expected;
    line = expr->sector.begin.line + 1;
    result_type = types.error_type();

    expr->visit(this);

    const Type* type = result_type;
    expected_type = saved_expected;
    line = saved_line;
    return type;
}

// Computes the static type of an expression without keeping its code. Used where the
// compiler needs a type before it may emit code, e.g. to infer template arguments.
const Type* Compiler::probe_type(Expr* expr)
{
    Chunk& code = chunk();
    size_t code_size = code.code.size();
    size_t constant_count = code.constants.size();

    diagnostics->mute();
    const Type* type = compile_expr(expr);
    diagnostics->unmute();

    code.code.resize(code_size);
    code.lines.resize(code_size);
    code.constants.resize(constant_count);
    code.constant_kinds.resize(constant_count);
    return type;
}

void Compiler::compile_stmt(Stmt* stmt)
{
    uint32_t saved_line = line;
    line = stmt->sector.begin.line + 1;
    stmt->visit(this);
    line = saved_line;
}

bool Compiler::is_assignable(const Type* from, const Type* to) const
{
    return from == to || from->kind == TypeKind::ERROR || to->kind == TypeKind::ERROR ||
           (from->kind == TypeKind::INT && to->kind == TypeKind::FLOAT) ||
           (from->kind == TypeKind::NULL_T && to->kind == TypeKind::CLASS);
}

// Converts the value on top of the stack from one type to another, if that is an
// allowed implicit conversion: int to float and null to any class type. Everything
// else needs an explicit cast<T>(...).
void Compiler::coerce(const Type* from, const Type* to, const SourceSector& sector,
                      const std::string& context)
{
    if (!is_assignable(from, to))
    {
        std::string hint;
        if (from->is_numeric() && to->is_numeric())
        {
            hint = " (use cast<" + to->name + ">(...))";
        }
        else if (from->kind == TypeKind::NULL_T)
        {
            hint = " (only class types can be null)";
        }
        diagnostics->error(sector, "Cannot convert " + quote(from->name) + " to " +
                                       quote(to->name) + " in " + context + hint + ".");
        return;
    }
    if (from->kind == TypeKind::INT && to->kind == TypeKind::FLOAT)
    {
        emit(OpCode::INT_TO_FLOAT);
    }
}

void Compiler::expect_bool(const Type* type, const SourceSector& sector,
                           const std::string& context)
{
    if (type->kind != TypeKind::BOOL && type->kind != TypeKind::ERROR)
    {
        diagnostics->error(sector,
                           context + " must be 'bool', got " + quote(type->name) + ".");
    }
}

// Emits the instruction for a binary operator whose operands are already on the stack.
// Mixed int/float operands are promoted to float.
const Type* Compiler::emit_binary_op(BinaryExpr::Op op, const Type* left,
                                     const Type* right, const SourceSector& sector)
{
    using Op = BinaryExpr::Op;
    if (left->kind == TypeKind::ERROR || right->kind == TypeKind::ERROR)
    {
        return types.error_type();
    }

    bool numeric = left->is_numeric() && right->is_numeric();
    auto promote = [&]() -> bool {
        // Returns true if the operation happens on floats.
        if (left->kind == TypeKind::INT && right->kind == TypeKind::FLOAT)
        {
            emit(OpCode::INT_TO_FLOAT_UNDER);
        }
        else if (left->kind == TypeKind::FLOAT && right->kind == TypeKind::INT)
        {
            emit(OpCode::INT_TO_FLOAT);
        }
        return left->kind == TypeKind::FLOAT || right->kind == TypeKind::FLOAT;
    };

    switch (op)
    {
    case Op::ADD:
        if (left->kind == TypeKind::STRING && right->kind == TypeKind::STRING)
        {
            emit(OpCode::CONCAT);
            return types.string_type();
        }
        [[fallthrough]];
    case Op::SUB:
    case Op::MUL:
    case Op::DIV:
        if (numeric)
        {
            bool is_float = promote();
            static const OpCode int_ops[] = {OpCode::ADD_INT, OpCode::SUB_INT,
                                             OpCode::MUL_INT, OpCode::DIV_INT};
            static const OpCode float_ops[] = {OpCode::ADD_FLOAT, OpCode::SUB_FLOAT,
                                               OpCode::MUL_FLOAT, OpCode::DIV_FLOAT};
            int index = static_cast<int>(op) - static_cast<int>(Op::ADD);
            emit(is_float ? float_ops[index] : int_ops[index]);
            return is_float ? types.float_type() : types.int_type();
        }
        break;
    case Op::MOD:
        if (left->kind == TypeKind::INT && right->kind == TypeKind::INT)
        {
            emit(OpCode::MOD_INT);
            return types.int_type();
        }
        break;
    case Op::LESS:
    case Op::LESS_EQUAL:
    case Op::GREATER:
    case Op::GREATER_EQUAL:
        if (numeric)
        {
            bool is_float = promote();
            static const OpCode int_ops[] = {OpCode::LT_INT, OpCode::LE_INT,
                                             OpCode::GT_INT, OpCode::GE_INT};
            static const OpCode float_ops[] = {OpCode::LT_FLOAT, OpCode::LE_FLOAT,
                                               OpCode::GT_FLOAT, OpCode::GE_FLOAT};
            int index = static_cast<int>(op) - static_cast<int>(Op::LESS);
            emit(is_float ? float_ops[index] : int_ops[index]);
            return types.bool_type();
        }
        if (left->kind == TypeKind::STRING && right->kind == TypeKind::STRING)
        {
            static const OpCode string_ops[] = {OpCode::LT_STRING, OpCode::LE_STRING,
                                                OpCode::GT_STRING, OpCode::GE_STRING};
            emit(string_ops[static_cast<int>(op) - static_cast<int>(Op::LESS)]);
            return types.bool_type();
        }
        break;
    case Op::EQUAL:
    case Op::NOT_EQUAL: {
        bool ok = true;
        if (numeric)
        {
            emit(promote() ? OpCode::EQ_FLOAT : OpCode::EQ_INT);
        }
        else if (left->kind == TypeKind::BOOL && right->kind == TypeKind::BOOL)
        {
            emit(OpCode::EQ_BOOL);
        }
        else if (left->kind == TypeKind::STRING && right->kind == TypeKind::STRING)
        {
            emit(OpCode::EQ_STRING);
        }
        else if ((left->kind == TypeKind::CLASS || left->kind == TypeKind::NULL_T) &&
                 (right->kind == TypeKind::CLASS || right->kind == TypeKind::NULL_T) &&
                 (left == right || left->kind == TypeKind::NULL_T ||
                  right->kind == TypeKind::NULL_T))
        {
            emit(OpCode::EQ_REF);
        }
        else if (left->kind == TypeKind::VECTOR && left == right)
        {
            emit(OpCode::EQ_REF);
        }
        else
        {
            ok = false;
        }
        if (ok)
        {
            if (op == Op::NOT_EQUAL)
            {
                emit(OpCode::NOT);
            }
            return types.bool_type();
        }
        break;
    }
    case Op::AND:
    case Op::OR:
        assert(false && "logical operators are compiled in visitBinary");
        break;
    }

    std::string hint;
    if (op == Op::ADD &&
        (left->kind == TypeKind::STRING || right->kind == TypeKind::STRING))
    {
        hint = " (convert with cast<string>(...) first)";
    }
    diagnostics->error(sector, std::string("Operator '") + binary_op_to_string(op) +
                                   "' cannot be applied to " + quote(left->name) +
                                   " and " + quote(right->name) + hint + ".");
    return types.error_type();
}

void Compiler::compile_args(const std::vector<ExprPtr>& args,
                            const std::vector<const Type*>& params, size_t first_param,
                            const SourceSector& sector, const std::string& callee)
{
    size_t expected = params.size() - first_param;
    if (args.size() != expected)
    {
        diagnostics->error(sector, quote(callee) + " expects " +
                                       std::to_string(expected) + " argument(s), got " +
                                       std::to_string(args.size()) + ".");
        return;
    }
    for (size_t i = 0; i < args.size(); i++)
    {
        const Type* param = params[first_param + i];
        const Type* type = compile_expr(args[i].get(), param);
        coerce(type, param, args[i]->sector,
               "argument " + std::to_string(i + 1) + " of " + quote(callee));
    }
}

const Type* Compiler::emit_call(FunctionInfo* function, const std::vector<ExprPtr>& args,
                                size_t first_param, const SourceSector& sector)
{
    auto lazy = lazy_bodies.find(function);
    if (lazy != lazy_bodies.end())
    {
        pending.push_back(std::move(lazy->second));
        lazy_bodies.erase(lazy);
    }
    compile_args(args, function->params, first_param, sector, function->name);
    emit(OpCode::CALL);
    emit_u16(function->index);
    return function->return_type;
}

// 'Point(1, 2)', 'Box<int>(5)', 'vector<int>()' or, inside templates, 'T()'.
const Type* Compiler::compile_construction(const Type* type,
                                           const std::vector<ExprPtr>& args,
                                           const SourceSector& sector)
{
    switch (type->kind)
    {
    case TypeKind::ERROR:
        return type;
    case TypeKind::CLASS: {
        auto* class_type = static_cast<const ClassType*>(type);
        emit(OpCode::NEW);
        emit_u16(class_type->class_index);
        return emit_call(class_type->constructor, args, 1, sector);
    }
    case TypeKind::VECTOR:
    case TypeKind::INT:
    case TypeKind::FLOAT:
    case TypeKind::BOOL:
    case TypeKind::STRING:
        if (!args.empty())
        {
            diagnostics->error(sector, quote(type->name) + "() does not take arguments.");
            return types.error_type();
        }
        emit_default(type);
        return type;
    default:
        diagnostics->error(sector,
                           "Cannot construct a value of type " + quote(type->name) + ".");
        return types.error_type();
    }
}

const Type* Compiler::compile_method_call(GetExpr* callee, CallExpr* call)
{
    const Type* receiver = compile_expr(callee->object.get());
    const std::string& name = callee->name;
    auto arity = [&](size_t count) {
        if (call->args.size() != count)
        {
            diagnostics->error(call->sector, quote(receiver->name + "::" + name) +
                                                 " expects " + std::to_string(count) +
                                                 " argument(s), got " +
                                                 std::to_string(call->args.size()) + ".");
            return false;
        }
        return true;
    };

    if (!call->type_args.empty())
    {
        diagnostics->error(call->sector, "Methods cannot take template arguments.");
        return types.error_type();
    }

    switch (receiver->kind)
    {
    case TypeKind::ERROR:
        return receiver;
    case TypeKind::CLASS: {
        auto* class_type = static_cast<const ClassType*>(receiver);
        auto method = class_type->methods.find(name);
        if (method != class_type->methods.end())
        {
            return emit_call(method->second, call->args, 1, call->sector);
        }
        if (class_type->find_field(name) >= 0)
        {
            diagnostics->error(callee->sector, quote(name) + " is a field of " +
                                                   quote(receiver->name) +
                                                   ", not a method.");
        }
        else
        {
            diagnostics->error(callee->sector, quote(receiver->name) + " has no method " +
                                                   quote(name) + ".");
        }
        return types.error_type();
    }
    case TypeKind::VECTOR: {
        const Type* element = static_cast<const VectorType*>(receiver)->element;
        if (name == "push")
        {
            if (arity(1))
            {
                const Type* type = compile_expr(call->args[0].get(), element);
                coerce(type, element, call->args[0]->sector, "argument of 'push'");
                emit(OpCode::VEC_PUSH);
            }
            return types.void_type();
        }
        if (name == "pop")
        {
            if (arity(0))
            {
                emit(OpCode::VEC_POP);
            }
            return element;
        }
        if (name == "size")
        {
            if (arity(0))
            {
                emit(OpCode::VEC_LEN);
            }
            return types.int_type();
        }
        if (name == "clear")
        {
            if (arity(0))
            {
                emit(OpCode::VEC_CLEAR);
            }
            return types.void_type();
        }
        diagnostics->error(callee->sector,
                           quote(receiver->name) + " has no method " + quote(name) +
                               " (vectors have push, pop, size and clear).");
        return types.error_type();
    }
    case TypeKind::STRING:
        if (name == "length")
        {
            if (arity(0))
            {
                emit(OpCode::STR_LEN);
            }
            return types.int_type();
        }
        diagnostics->error(callee->sector, "'string' has no method " + quote(name) +
                                               " (strings have length).");
        return types.error_type();
    case TypeKind::NULL_T:
        diagnostics->error(callee->sector, "Cannot call a method on 'null'.");
        return types.error_type();
    default:
        diagnostics->error(callee->sector,
                           "Type " + quote(receiver->name) + " has no methods.");
        return types.error_type();
    }
}

// Matches a parameter type as written ('vector<T>') against an argument type
// ('vector<int>') and records what each template parameter must be ('T = int').
void Compiler::unify(const TypeExpr& param, const Type* actual,
                     const std::vector<std::string>& names, TypeSubstitution& bound)
{
    if (actual->kind == TypeKind::ERROR || actual->kind == TypeKind::NULL_T)
    {
        return;
    }
    if (param.args.empty())
    {
        for (const std::string& name : names)
        {
            if (name == param.name && !bound.count(name))
            {
                bound[name] = actual;
            }
        }
        return;
    }
    if (param.name == "vector" && actual->kind == TypeKind::VECTOR &&
        param.args.size() == 1)
    {
        unify(*param.args[0], static_cast<const VectorType*>(actual)->element, names,
              bound);
        return;
    }
    if (actual->kind == TypeKind::CLASS)
    {
        auto* class_type = static_cast<const ClassType*>(actual);
        if (class_type->decl->name == param.name &&
            class_type->type_args.size() == param.args.size())
        {
            for (size_t i = 0; i < param.args.size(); i++)
            {
                unify(*param.args[i], class_type->type_args[i], names, bound);
            }
        }
    }
}

const Type* Compiler::compile_template_call(FuncDecl* decl, CallExpr* call)
{
    std::vector<const Type*> type_args;
    if (!call->type_args.empty())
    {
        if (call->type_args.size() != decl->type_params.size())
        {
            diagnostics->error(call->sector,
                               "Template " + quote(decl->name) + " expects " +
                                   std::to_string(decl->type_params.size()) +
                                   " template argument(s), got " +
                                   std::to_string(call->type_args.size()) + ".");
            return types.error_type();
        }
        for (auto& arg : call->type_args)
        {
            const Type* type = resolve_value_type(*arg, ctx->substitution, ctx->owner,
                                                  "a template argument");
            if (type->kind == TypeKind::ERROR)
            {
                return type;
            }
            type_args.push_back(type);
        }
    }
    else
    {
        // Infer the template arguments from the argument types, like C++ does.
        if (call->args.size() != decl->params.size())
        {
            diagnostics->error(call->sector, quote(decl->name) + " expects " +
                                                 std::to_string(decl->params.size()) +
                                                 " argument(s), got " +
                                                 std::to_string(call->args.size()) + ".");
            return types.error_type();
        }
        TypeSubstitution bound;
        for (size_t i = 0; i < call->args.size(); i++)
        {
            const Type* arg = probe_type(call->args[i].get());
            if (arg->kind == TypeKind::ERROR)
            {
                // Compile it for real to report the actual error instead of a confusing
                // "cannot infer" message.
                compile_expr(call->args[i].get());
                return types.error_type();
            }
            unify(*decl->params[i].type, arg, decl->type_params, bound);
        }
        for (const std::string& name : decl->type_params)
        {
            if (!bound.count(name))
            {
                diagnostics->error(call->sector, "Cannot infer template argument " +
                                                     quote(name) + " of " +
                                                     quote(decl->name) +
                                                     "; write it explicitly, like " +
                                                     decl->name + "<int>(...).");
                return types.error_type();
            }
            type_args.push_back(bound[name]);
        }
    }

    FunctionInfo* function = instantiate_function(decl, type_args, call->sector);
    if (!function)
    {
        return types.error_type();
    }
    return emit_call(function, call->args, 0, call->sector);
}

// Emits the part of an assignment target that has to be evaluated before the value:
// nothing for variables, the object for fields, and the vector and index for elements.
Compiler::LValue Compiler::compile_lvalue_prefix(Expr* target)
{
    LValue result;
    result.type = types.error_type();

    if (auto* var = dynamic_cast<VarExpr*>(target))
    {
        int slot = resolve_local(var->name);
        if (slot >= 0)
        {
            result.kind = LValue::Kind::LOCAL;
            result.type = ctx->locals[slot].type;
            result.index = static_cast<uint16_t>(slot);
            return result;
        }
        if (ctx->owner)
        {
            int field = ctx->owner->find_field(var->name);
            if (field >= 0)
            {
                emit(OpCode::GET_LOCAL);
                emit_byte(0);
                result.kind = LValue::Kind::FIELD;
                result.type = ctx->owner->fields[field].type;
                result.index = static_cast<uint16_t>(field);
                return result;
            }
        }
        auto global = globals.find(var->name);
        if (global != globals.end())
        {
            if (ctx->is_script && !global->second.defined)
            {
                diagnostics->error(var->sector, "Use of global " + quote(var->name) +
                                                    " before its declaration.");
            }
            result.kind = LValue::Kind::GLOBAL;
            result.type = global->second.type;
            result.index = global->second.index;
            return result;
        }
        if (function_decls.count(var->name) ||
            (ctx->owner && ctx->owner->methods.count(var->name)))
        {
            diagnostics->error(var->sector,
                               quote(var->name) +
                                   " is a function; functions are not values.");
        }
        else if (class_decls.count(var->name) || var->name == "vector")
        {
            diagnostics->error(var->sector,
                               quote(var->name) + " is a type, not a value.");
        }
        else
        {
            diagnostics->error(var->sector, "Unknown variable " + quote(var->name) + ".");
        }
        return result;
    }

    if (auto* get = dynamic_cast<GetExpr*>(target))
    {
        const Type* object = compile_expr(get->object.get());
        if (object->kind == TypeKind::ERROR)
        {
            return result;
        }
        if (object->kind == TypeKind::CLASS)
        {
            auto* class_type = static_cast<const ClassType*>(object);
            int field = class_type->find_field(get->name);
            if (field >= 0)
            {
                result.kind = LValue::Kind::FIELD;
                result.type = class_type->fields[field].type;
                result.index = static_cast<uint16_t>(field);
                return result;
            }
            if (class_type->methods.count(get->name))
            {
                diagnostics->error(get->sector, "Method " + quote(get->name) +
                                                    " must be called, like " + get->name +
                                                    "().");
            }
            else
            {
                diagnostics->error(get->sector, quote(object->name) + " has no field " +
                                                    quote(get->name) + ".");
            }
            return result;
        }
        if (object->kind == TypeKind::VECTOR || object->kind == TypeKind::STRING)
        {
            diagnostics->error(get->sector, quote(object->name) + " has no field " +
                                                quote(get->name) + " (did you mean " +
                                                get->name + "()?).");
            return result;
        }
        diagnostics->error(get->sector,
                           "Type " + quote(object->name) + " has no fields.");
        return result;
    }

    if (auto* index = dynamic_cast<IndexExpr*>(target))
    {
        const Type* object = compile_expr(index->object.get());
        const Type* index_type = compile_expr(index->index.get());
        if (object->kind == TypeKind::ERROR)
        {
            return result;
        }
        if (object->kind != TypeKind::VECTOR)
        {
            diagnostics->error(index->object->sector,
                               "Only vectors can be indexed, not " + quote(object->name) +
                                   ".");
            return result;
        }
        if (index_type->kind != TypeKind::INT && index_type->kind != TypeKind::ERROR)
        {
            diagnostics->error(index->index->sector, "Vector index must be 'int', got " +
                                                         quote(index_type->name) + ".");
        }
        result.kind = LValue::Kind::ELEMENT;
        result.type = static_cast<const VectorType*>(object)->element;
        return result;
    }

    // The parser only allows the three kinds of targets above in assignments, but
    // '++' and '--' accept any expression.
    compile_expr(target);
    diagnostics->error(target->sector, "Expression is not assignable.");
    return result;
}

// Compiles 'target = rhs' or 'target op= rhs'. 'rhs' emits the right-hand side; it gets
// the target type as a hint.
const Type* Compiler::compile_assignment(
    Expr* target, std::optional<BinaryExpr::Op> op,
    const std::function<const Type*(const Type*)>& rhs, const SourceSector& sector)
{
    LValue lvalue = compile_lvalue_prefix(target);
    if (lvalue.kind == LValue::Kind::INVALID)
    {
        rhs(nullptr);
        return types.error_type();
    }

    const Type* value;
    if (op)
    {
        // Load the current value. For fields and elements, duplicate the object (and
        // index) first so they are still there for the store.
        switch (lvalue.kind)
        {
        case LValue::Kind::LOCAL:
            emit(OpCode::GET_LOCAL);
            emit_byte(static_cast<uint8_t>(lvalue.index));
            break;
        case LValue::Kind::GLOBAL:
            emit(OpCode::GET_GLOBAL);
            emit_u16(lvalue.index);
            break;
        case LValue::Kind::FIELD:
            emit(OpCode::DUP);
            emit(OpCode::GET_FIELD);
            emit_u16(lvalue.index);
            break;
        case LValue::Kind::ELEMENT:
            emit(OpCode::DUP2);
            emit(OpCode::VEC_GET);
            break;
        case LValue::Kind::INVALID:
            break;
        }
        const Type* right = rhs(lvalue.type);
        value = emit_binary_op(*op, lvalue.type, right, sector);
    }
    else
    {
        value = rhs(lvalue.type);
    }
    coerce(value, lvalue.type, sector, "assignment");

    switch (lvalue.kind)
    {
    case LValue::Kind::LOCAL:
        emit(OpCode::SET_LOCAL);
        emit_byte(static_cast<uint8_t>(lvalue.index));
        break;
    case LValue::Kind::GLOBAL:
        emit(OpCode::SET_GLOBAL);
        emit_u16(lvalue.index);
        break;
    case LValue::Kind::FIELD:
        emit(OpCode::SET_FIELD);
        emit_u16(lvalue.index);
        break;
    case LValue::Kind::ELEMENT:
        emit(OpCode::VEC_SET);
        break;
    case LValue::Kind::INVALID:
        break;
    }
    return lvalue.type;
}

// ==================================================================================
// Expressions: visitors
// ==================================================================================

void Compiler::visitInt(IntExpr* expr)
{
    emit_constant(int_value(expr->value), ValueKind::INT);
    result_type = types.int_type();
}

void Compiler::visitFloat(FloatExpr* expr)
{
    emit_constant(float_value(expr->value), ValueKind::FLOAT);
    result_type = types.float_type();
}

void Compiler::visitBool(BoolExpr* expr)
{
    emit(expr->value ? OpCode::TRUE : OpCode::FALSE);
    result_type = types.bool_type();
}

void Compiler::visitString(StringExpr* expr)
{
    emit_string(expr->value);
    result_type = types.string_type();
}

void Compiler::visitNull(NullExpr*)
{
    emit(OpCode::NULL_REF);
    result_type = types.null_type();
}

void Compiler::visitThis(ThisExpr* expr)
{
    if (!ctx->owner)
    {
        diagnostics->error(expr->sector,
                           "'this' can only be used inside methods and constructors.");
        return;
    }
    emit(OpCode::GET_LOCAL);
    emit_byte(0);
    result_type = ctx->owner;
}

void Compiler::visitVar(VarExpr* expr)
{
    LValue lvalue = compile_lvalue_prefix(expr);
    switch (lvalue.kind)
    {
    case LValue::Kind::LOCAL:
        emit(OpCode::GET_LOCAL);
        emit_byte(static_cast<uint8_t>(lvalue.index));
        break;
    case LValue::Kind::GLOBAL:
        emit(OpCode::GET_GLOBAL);
        emit_u16(lvalue.index);
        break;
    case LValue::Kind::FIELD:
        emit(OpCode::GET_FIELD);
        emit_u16(lvalue.index);
        break;
    default:
        break;
    }
    result_type = lvalue.type;
}

void Compiler::visitGet(GetExpr* expr)
{
    LValue lvalue = compile_lvalue_prefix(expr);
    if (lvalue.kind == LValue::Kind::FIELD)
    {
        emit(OpCode::GET_FIELD);
        emit_u16(lvalue.index);
    }
    result_type = lvalue.type;
}

void Compiler::visitIndex(IndexExpr* expr)
{
    LValue lvalue = compile_lvalue_prefix(expr);
    if (lvalue.kind == LValue::Kind::ELEMENT)
    {
        emit(OpCode::VEC_GET);
    }
    result_type = lvalue.type;
}

void Compiler::visitUnary(UnaryExpr* expr)
{
    const Type* type = compile_expr(expr->operand.get());
    if (type->kind == TypeKind::ERROR)
    {
        return;
    }
    if (expr->op == UnaryExpr::Op::NEGATE)
    {
        if (type->kind == TypeKind::INT)
        {
            emit(OpCode::NEG_INT);
        }
        else if (type->kind == TypeKind::FLOAT)
        {
            emit(OpCode::NEG_FLOAT);
        }
        else
        {
            diagnostics->error(expr->sector,
                               "Operator '-' cannot be applied to " + quote(type->name) +
                                   (type->kind == TypeKind::BOOL ? " (use '!')." : "."));
            return;
        }
        result_type = type;
        return;
    }
    if (type->kind != TypeKind::BOOL)
    {
        diagnostics->error(expr->sector, "Operator '!' cannot be applied to " +
                                             quote(type->name) + ".");
        return;
    }
    emit(OpCode::NOT);
    result_type = type;
}

void Compiler::visitBinary(BinaryExpr* expr)
{
    if (expr->op == BinaryExpr::Op::AND || expr->op == BinaryExpr::Op::OR)
    {
        // Short circuit: 'a && b' skips 'b' if 'a' is false, leaving 'false' as result.
        const Type* left = compile_expr(expr->left.get());
        expect_bool(left, expr->left->sector, "Operand of '&&' and '||'");
        size_t jump =
            emit_jump(expr->op == BinaryExpr::Op::AND ? OpCode::JUMP_IF_FALSE_KEEP
                                                      : OpCode::JUMP_IF_TRUE_KEEP);
        emit(OpCode::POP);
        const Type* right = compile_expr(expr->right.get());
        expect_bool(right, expr->right->sector, "Operand of '&&' and '||'");
        patch_jump(jump);
        result_type = types.bool_type();
        return;
    }

    const Type* left = compile_expr(expr->left.get());
    const Type* right = compile_expr(expr->right.get());
    result_type = emit_binary_op(expr->op, left, right, expr->sector);
}

void Compiler::visitAssign(AssignExpr* expr)
{
    result_type = compile_assignment(
        expr->target.get(), expr->op,
        [&](const Type* target) { return compile_expr(expr->value.get(), target); },
        expr->sector);
}

void Compiler::visitIncrement(IncrementExpr* expr)
{
    const Type* type = compile_assignment(
        expr->target.get(), BinaryExpr::Op::ADD,
        [&](const Type* target) {
            if (target && target->kind == TypeKind::FLOAT)
            {
                emit_constant(float_value(expr->delta), ValueKind::FLOAT);
                return types.float_type();
            }
            emit_constant(int_value(expr->delta), ValueKind::INT);
            return types.int_type();
        },
        expr->sector);

    if (!type->is_numeric())
    {
        if (type->kind != TypeKind::ERROR)
        {
            diagnostics->error(expr->sector,
                               "'++' and '--' need an 'int' or 'float', got " +
                                   quote(type->name) + ".");
        }
        result_type = types.error_type();
        return;
    }
    if (!expr->prefix)
    {
        // The stored value is the new one; 'x++' evaluates to the old one.
        if (type->kind == TypeKind::FLOAT)
        {
            emit_constant(float_value(expr->delta), ValueKind::FLOAT);
            emit(OpCode::SUB_FLOAT);
        }
        else
        {
            emit_constant(int_value(expr->delta), ValueKind::INT);
            emit(OpCode::SUB_INT);
        }
    }
    result_type = type;
}

void Compiler::visitCall(CallExpr* expr)
{
    if (auto* get = dynamic_cast<GetExpr*>(expr->callee.get()))
    {
        result_type = compile_method_call(get, expr);
        return;
    }
    auto* var = dynamic_cast<VarExpr*>(expr->callee.get());
    if (!var)
    {
        diagnostics->error(expr->callee->sector, "Expression is not callable.");
        return;
    }
    const std::string& name = var->name;

    // A method of the current class, called without 'this.'.
    if (ctx->owner && expr->type_args.empty())
    {
        auto method = ctx->owner->methods.find(name);
        if (method != ctx->owner->methods.end())
        {
            emit(OpCode::GET_LOCAL);
            emit_byte(0);
            result_type = emit_call(method->second, expr->args, 1, expr->sector);
            return;
        }
    }

    // A free function or function template.
    auto function = function_decls.find(name);
    if (function != function_decls.end())
    {
        FuncDecl* decl = function->second;
        if (decl->type_params.empty())
        {
            if (!expr->type_args.empty())
            {
                diagnostics->error(expr->sector, quote(name) + " is not a template.");
                return;
            }
            FunctionInfo* info = instantiate_function(decl, {}, expr->sector);
            if (info)
            {
                result_type = emit_call(info, expr->args, 0, expr->sector);
            }
            return;
        }
        result_type = compile_template_call(decl, expr);
        return;
    }

    // A type: construct a value of it.
    bool is_type = name == "vector" || class_decls.count(name) ||
                   (ctx->substitution && ctx->substitution->count(name));
    if (is_type)
    {
        TypeExpr type_expr;
        type_expr.name = name;
        type_expr.sector = var->sector;
        // Borrow the type arguments for resolving, then give them back.
        type_expr.args = std::move(expr->type_args);
        const Type* type = resolve_type(type_expr);
        expr->type_args = std::move(type_expr.args);
        result_type = compile_construction(type, expr->args, expr->sector);
        return;
    }

    if (resolve_local(name) >= 0 || globals.count(name) ||
        (ctx->owner && ctx->owner->find_field(name) >= 0))
    {
        diagnostics->error(var->sector, quote(name) + " is a variable, not a function.");
    }
    else
    {
        diagnostics->error(var->sector, "Unknown function " + quote(name) + ".");
    }
}

void Compiler::visitCast(CastExpr* expr)
{
    const Type* target =
        resolve_value_type(*expr->type, ctx->substitution, ctx->owner, "a cast");
    const Type* from = compile_expr(expr->expr.get());
    result_type = target;
    if (from == target || from->kind == TypeKind::ERROR ||
        target->kind == TypeKind::ERROR)
    {
        return;
    }

    TypeKind f = from->kind;
    TypeKind t = target->kind;
    if (f == TypeKind::INT && t == TypeKind::FLOAT)
    {
        emit(OpCode::INT_TO_FLOAT);
    }
    else if (f == TypeKind::FLOAT && t == TypeKind::INT)
    {
        emit(OpCode::FLOAT_TO_INT);
    }
    else if (f == TypeKind::INT && t == TypeKind::BOOL)
    {
        emit(OpCode::INT_TO_BOOL);
    }
    else if (f == TypeKind::BOOL && t == TypeKind::INT)
    {
        emit(OpCode::BOOL_TO_INT);
    }
    else if (f == TypeKind::INT && t == TypeKind::STRING)
    {
        emit(OpCode::INT_TO_STRING);
    }
    else if (f == TypeKind::FLOAT && t == TypeKind::STRING)
    {
        emit(OpCode::FLOAT_TO_STRING);
    }
    else if (f == TypeKind::BOOL && t == TypeKind::STRING)
    {
        emit(OpCode::BOOL_TO_STRING);
    }
    else if (f == TypeKind::NULL_T && t == TypeKind::CLASS)
    {
        // null is already a valid value of every class type
    }
    else
    {
        diagnostics->error(expr->sector, "Cannot cast " + quote(from->name) + " to " +
                                             quote(target->name) + ".");
        result_type = types.error_type();
    }
}

void Compiler::visitVector(VectorExpr* expr)
{
    const Type* element = nullptr;
    if (expected_type && expected_type->kind == TypeKind::VECTOR)
    {
        element = static_cast<const VectorType*>(expected_type)->element;
    }
    else if (expr->elements.empty())
    {
        diagnostics->error(expr->sector, "Cannot infer the type of an empty vector "
                                         "literal; write vector<T>() instead.");
        return;
    }
    else
    {
        element = probe_type(expr->elements[0].get());
        if (element->kind == TypeKind::NULL_T || element->kind == TypeKind::VOID)
        {
            diagnostics->error(
                expr->elements[0]->sector,
                "Cannot infer the element type of this vector literal from " +
                    quote(element->name) + ".");
            return;
        }
        if (element->kind == TypeKind::ERROR)
        {
            return;
        }
    }

    emit(OpCode::VEC_NEW);
    emit_byte(static_cast<uint8_t>(element->value_kind()));
    for (auto& item : expr->elements)
    {
        emit(OpCode::DUP);
        const Type* type = compile_expr(item.get(), element);
        coerce(type, element, item->sector, "vector element");
        emit(OpCode::VEC_PUSH);
    }
    result_type = types.vector_of(element);
}

// ==================================================================================
// Statements
// ==================================================================================

void Compiler::visitExprStmt(ExprStmt* stmt)
{
    const Type* type = compile_expr(stmt->expr.get());
    if (type->kind == TypeKind::VOID)
    {
        return;
    }
    // In the REPL, top-level expressions print their value. Assignments stay quiet.
    bool is_assignment = dynamic_cast<AssignExpr*>(stmt->expr.get()) ||
                         dynamic_cast<IncrementExpr*>(stmt->expr.get());
    if (repl_mode && ctx->is_script && ctx->scope_depth == 0 && !is_assignment)
    {
        emit_print(type, stmt->expr->sector);
        return;
    }
    emit(OpCode::POP);
}

void Compiler::visitPrint(PrintStmt* stmt)
{
    emit_print(compile_expr(stmt->expr.get()), stmt->expr->sector);
}

void Compiler::emit_print(const Type* type, const SourceSector& sector)
{
    switch (type->kind)
    {
    case TypeKind::ERROR:
        break;
    case TypeKind::VOID:
        diagnostics->error(sector, "Cannot print a 'void' value.");
        break;
    case TypeKind::INT:
        emit(OpCode::PRINT_INT);
        break;
    case TypeKind::FLOAT:
        emit(OpCode::PRINT_FLOAT);
        break;
    case TypeKind::BOOL:
        emit(OpCode::PRINT_BOOL);
        break;
    default:
        emit(OpCode::PRINT_REF);
        break;
    }
}

void Compiler::visitVarStmt(VarStmt* stmt)
{
    bool is_global = ctx->is_script && ctx->scope_depth == 0;
    bool is_auto = stmt->type->name == "auto" && stmt->type->args.empty();
    const std::string what = quote(stmt->name);

    const Type* type;
    if (is_auto)
    {
        if (!stmt->initializer)
        {
            diagnostics->error(stmt->sector,
                               "'auto' variable " + what + " needs an initializer.");
            type = types.error_type();
            emit(OpCode::NULL_REF);
        }
        else
        {
            type = compile_expr(stmt->initializer.get());
            if (type->kind == TypeKind::NULL_T || type->kind == TypeKind::VOID)
            {
                diagnostics->error(stmt->initializer->sector,
                                   "Cannot infer the type of " + what + " from " +
                                       quote(type->name) + ".");
                type = types.error_type();
            }
        }
    }
    else
    {
        if (is_global && globals.count(stmt->name))
        {
            type = globals[stmt->name].type; // resolved when the global was pre-declared
        }
        else
        {
            type = resolve_value_type(*stmt->type, ctx->substitution, ctx->owner,
                                      "variable " + what);
        }
        if (stmt->initializer)
        {
            const Type* init = compile_expr(stmt->initializer.get(), type);
            coerce(init, type, stmt->initializer->sector, "initializer of " + what);
        }
        else
        {
            emit_default(type);
        }
    }

    if (!is_global)
    {
        declare_local(stmt->name, type, stmt->sector);
        return;
    }

    if (is_auto)
    {
        if (globals.count(stmt->name))
        {
            diagnostics->error(stmt->sector,
                               "Redefinition of global variable " + what + ".");
        }
        else
        {
            uint16_t index = static_cast<uint16_t>(program->global_names.size());
            program->global_names.push_back(stmt->name);
            program->global_kinds.push_back(type->value_kind());
            globals[stmt->name] = Global{type, index, false};
        }
    }
    Global& global = globals[stmt->name];
    emit(OpCode::SET_GLOBAL);
    emit_u16(global.index);
    emit(OpCode::POP);
    global.defined = true;
}

void Compiler::visitBlock(BlockStmt* stmt)
{
    begin_scope();
    for (auto& statement : stmt->statements)
    {
        compile_stmt(statement.get());
    }
    end_scope();
}

void Compiler::visitIf(IfStmt* stmt)
{
    const Type* condition = compile_expr(stmt->condition.get());
    expect_bool(condition, stmt->condition->sector, "Condition of 'if'");
    size_t then_jump = emit_jump(OpCode::JUMP_IF_FALSE);

    begin_scope();
    compile_stmt(stmt->then_branch.get());
    end_scope();

    if (stmt->else_branch)
    {
        size_t else_jump = emit_jump(OpCode::JUMP);
        patch_jump(then_jump);
        begin_scope();
        compile_stmt(stmt->else_branch.get());
        end_scope();
        patch_jump(else_jump);
    }
    else
    {
        patch_jump(then_jump);
    }
}

void Compiler::visitWhile(WhileStmt* stmt)
{
    size_t loop_start = chunk().code.size();
    const Type* condition = compile_expr(stmt->condition.get());
    expect_bool(condition, stmt->condition->sector, "Condition of 'while'");
    size_t exit_jump = emit_jump(OpCode::JUMP_IF_FALSE);

    ctx->loops.push_back(Loop{ctx->scope_depth, {}, {}});
    begin_scope();
    compile_stmt(stmt->body.get());
    end_scope();

    for (size_t jump : ctx->loops.back().continue_jumps)
    {
        patch_jump(jump);
    }
    emit_loop(loop_start);
    patch_jump(exit_jump);
    for (size_t jump : ctx->loops.back().break_jumps)
    {
        patch_jump(jump);
    }
    ctx->loops.pop_back();
}

void Compiler::visitFor(ForStmt* stmt)
{
    begin_scope(); // the scope of the loop variable
    if (stmt->initializer)
    {
        compile_stmt(stmt->initializer.get());
    }

    size_t loop_start = chunk().code.size();
    std::optional<size_t> exit_jump;
    if (stmt->condition)
    {
        const Type* condition = compile_expr(stmt->condition.get());
        expect_bool(condition, stmt->condition->sector, "Condition of 'for'");
        exit_jump = emit_jump(OpCode::JUMP_IF_FALSE);
    }

    ctx->loops.push_back(Loop{ctx->scope_depth, {}, {}});
    begin_scope();
    compile_stmt(stmt->body.get());
    end_scope();

    for (size_t jump : ctx->loops.back().continue_jumps)
    {
        patch_jump(jump);
    }
    if (stmt->increment)
    {
        if (compile_expr(stmt->increment.get())->kind != TypeKind::VOID)
        {
            emit(OpCode::POP);
        }
    }
    emit_loop(loop_start);
    if (exit_jump)
    {
        patch_jump(*exit_jump);
    }
    for (size_t jump : ctx->loops.back().break_jumps)
    {
        patch_jump(jump);
    }
    ctx->loops.pop_back();
    end_scope();
}

void Compiler::visitReturn(ReturnStmt* stmt)
{
    if (ctx->is_script)
    {
        if (stmt->value)
        {
            diagnostics->error(stmt->sector, "Top-level code cannot return a value.");
        }
        emit(OpCode::RETURN_VOID);
        return;
    }
    if (ctx->is_constructor)
    {
        if (stmt->value)
        {
            diagnostics->error(stmt->sector, "A constructor cannot return a value.");
        }
        emit(OpCode::GET_LOCAL);
        emit_byte(0);
        emit(OpCode::RETURN);
        return;
    }

    const Type* return_type = ctx->info->return_type;
    if (return_type->kind == TypeKind::VOID)
    {
        if (stmt->value)
        {
            diagnostics->error(stmt->value->sector,
                               "Function " + quote(ctx->info->name) +
                                   " returns 'void' and cannot return a value.");
        }
        emit(OpCode::RETURN_VOID);
        return;
    }
    if (!stmt->value)
    {
        diagnostics->error(stmt->sector, "Function " + quote(ctx->info->name) +
                                             " must return a " +
                                             quote(return_type->name) + ".");
        return;
    }
    const Type* type = compile_expr(stmt->value.get(), return_type);
    coerce(type, return_type, stmt->value->sector,
           "return value of " + quote(ctx->info->name));
    emit(OpCode::RETURN);
}

void Compiler::visitBreak(BreakStmt* stmt)
{
    if (ctx->loops.empty())
    {
        diagnostics->error(stmt->sector, "'break' outside of a loop.");
        return;
    }
    emit_pops(locals_above(ctx->loops.back().scope_depth));
    ctx->loops.back().break_jumps.push_back(emit_jump(OpCode::JUMP));
}

void Compiler::visitContinue(ContinueStmt* stmt)
{
    if (ctx->loops.empty())
    {
        diagnostics->error(stmt->sector, "'continue' outside of a loop.");
        return;
    }
    emit_pops(locals_above(ctx->loops.back().scope_depth));
    ctx->loops.back().continue_jumps.push_back(emit_jump(OpCode::JUMP));
}

void Compiler::visitFuncDecl(FuncDecl*)
{
    // Functions are collected and compiled by compile(); the parser does not allow
    // nested functions.
}

void Compiler::visitClassDecl(ClassDecl*)
{
    // Classes are instantiated on demand; see instantiate_class.
}

// A function must not be able to reach its end without returning a value.
bool Compiler::always_returns(Stmt* stmt) const
{
    if (dynamic_cast<ReturnStmt*>(stmt))
    {
        return true;
    }
    if (auto* block = dynamic_cast<BlockStmt*>(stmt))
    {
        for (auto& statement : block->statements)
        {
            if (always_returns(statement.get()))
            {
                return true;
            }
        }
        return false;
    }
    if (auto* if_stmt = dynamic_cast<IfStmt*>(stmt))
    {
        return if_stmt->else_branch && always_returns(if_stmt->then_branch.get()) &&
               always_returns(if_stmt->else_branch.get());
    }
    // 'while (true)' and 'for (;;)' without 'break' never fall through.
    if (auto* while_stmt = dynamic_cast<WhileStmt*>(stmt))
    {
        auto* condition = dynamic_cast<BoolExpr*>(while_stmt->condition.get());
        return condition && condition->value && !contains_break(while_stmt->body.get());
    }
    if (auto* for_stmt = dynamic_cast<ForStmt*>(stmt))
    {
        return !for_stmt->condition && !contains_break(for_stmt->body.get());
    }
    return false;
}

bool Compiler::contains_break(Stmt* stmt) const
{
    if (dynamic_cast<BreakStmt*>(stmt))
    {
        return true;
    }
    if (auto* block = dynamic_cast<BlockStmt*>(stmt))
    {
        for (auto& statement : block->statements)
        {
            if (contains_break(statement.get()))
            {
                return true;
            }
        }
        return false;
    }
    if (auto* if_stmt = dynamic_cast<IfStmt*>(stmt))
    {
        return contains_break(if_stmt->then_branch.get()) ||
               (if_stmt->else_branch && contains_break(if_stmt->else_branch.get()));
    }
    // A 'break' inside a nested loop belongs to that loop.
    return false;
}
