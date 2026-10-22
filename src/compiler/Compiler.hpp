#pragma once

#include "ast/Expr.hpp"
#include "ast/Stmt.hpp"
#include "bytecode/Program.hpp"
#include "common/Diagnostics.hpp"
#include "types/Type.hpp"

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>

// Type checks the AST and emits bytecode in a single pass.
//
// Every visitExpr method emits the code for an expression and leaves the expression's
// static type in 'result_type' (see compile_expr). Because type checking happens while
// emitting code, compiling the same template body with a different substitution
// ('T = int', then 'T = string') naturally produces differently typed code. That is
// how templates are implemented: by monomorphization, like C++.
//
// Compilation order:
//   1. Collect all top-level class and function declarations by name, so everything can
//      be used before it is declared.
//   2. Instantiate all non-template classes and declare all non-template functions.
//      Their bodies are queued in 'pending'.
//   3. Compile the top-level statements into the script function.
//   4. Drain 'pending'. Compiling a body can instantiate new templates, which queues
//      more bodies, until everything that is used has been compiled.
class Compiler : public VisitorExpr, public VisitorStmt
{
  public:
    explicit Compiler(Diagnostics& diagnostics);

    // Compiles a module into the program and returns the index of the function that
    // runs its top-level statements. Can be called repeatedly (the REPL does this for
    // every entry): later modules see the classes, functions and globals of earlier
    // ones. On a compile error, everything this call declared is rolled back.
    std::optional<uint16_t> compile(std::vector<StmtPtr>& module);
    Program& get_program();

    void set_diagnostics(Diagnostics& diagnostics);
    // In REPL mode, top-level expression statements print their value.
    void set_repl_mode(bool enabled);

  private:
    struct Local
    {
        std::string name;
        const Type* type;
        int depth;
    };

    struct Loop
    {
        int scope_depth;
        std::vector<size_t> break_jumps;
        std::vector<size_t> continue_jumps;
    };

    // State of the function that is currently being compiled.
    struct FunctionCtx
    {
        FunctionInfo* info = nullptr;
        Function* function = nullptr;
        ClassType* owner = nullptr; // set inside methods and constructors
        const TypeSubstitution* substitution = nullptr;
        std::vector<Local> locals;
        int scope_depth = 0;
        std::vector<Loop> loops;
        bool is_script = false;
        bool is_constructor = false;
    };

    struct Global
    {
        const Type* type;
        uint16_t index;
        bool defined; // the script has executed the declaration
    };

    struct PendingBody
    {
        FunctionInfo* info;
        FuncDecl* decl;
        ClassType* owner;
        TypeSubstitution substitution;
    };

    // Something that can be assigned to: a local, a global, a field or a vector element.
    struct LValue
    {
        enum class Kind
        {
            LOCAL,
            GLOBAL,
            FIELD,
            ELEMENT,
            INVALID,
        };
        Kind kind = Kind::INVALID;
        const Type* type = nullptr;
        uint16_t index = 0;
    };

    Diagnostics* diagnostics;
    TypeTable types;
    std::unique_ptr<Program> program;

    std::unordered_map<std::string, ClassDecl*> class_decls;
    std::unordered_map<std::string, FuncDecl*> function_decls;
    std::map<std::pair<FuncDecl*, std::vector<const Type*>>, FunctionInfo*>
        function_instances;
    std::vector<std::unique_ptr<FunctionInfo>> function_infos;
    std::unordered_map<std::string, Global> globals;
    std::unordered_map<std::string, uint16_t> string_constants;
    std::deque<PendingBody> pending;
    // Bodies of class template members that have not been called yet. Like in C++, a
    // member of a class template is only compiled once it is used, so 'Box<string>'
    // works even if Box has a method that only makes sense for numbers.
    std::unordered_map<FunctionInfo*, PendingBody> lazy_bodies;

    bool repl_mode = false;
    std::vector<std::string> new_declarations; // class/function names added by compile()

    struct Checkpoint
    {
        size_t functions;
        size_t classes;
        size_t globals;
        size_t type_classes;
        std::unordered_map<FunctionInfo*, PendingBody> lazy_bodies;
    };
    Checkpoint make_checkpoint() const;
    void rollback(Checkpoint& checkpoint);
    int instantiation_depth = 0;

    FunctionCtx* ctx = nullptr;
    const Type* result_type = nullptr;
    const Type* expected_type = nullptr; // optional hint for '[]' and 'null'
    uint32_t line = 0;

    // ---- declarations and templates ----
    void collect_declarations(std::vector<StmtPtr>& module);
    const Type* resolve_type(const TypeExpr& type_expr,
                             const TypeSubstitution* substitution, ClassType* owner);
    const Type* resolve_type(const TypeExpr& type_expr);
    const Type* resolve_value_type(const TypeExpr& type_expr,
                                   const TypeSubstitution* substitution, ClassType* owner,
                                   const std::string& what);
    ClassType* instantiate_class(ClassDecl* decl, const std::vector<const Type*>& args,
                                 const SourceSector& where);
    FunctionInfo* instantiate_function(FuncDecl* decl,
                                       const std::vector<const Type*>& args,
                                       const SourceSector& where);
    FunctionInfo* new_function(const std::string& name, std::vector<const Type*> params,
                               const Type* return_type, ClassType* owner, FuncDecl* decl);
    void compile_body(PendingBody& body);
    void queue_body(PendingBody body);

    // ---- emitting code ----
    Chunk& chunk();
    void emit(OpCode op);
    void emit_byte(uint8_t byte);
    void emit_u16(uint16_t value);
    void emit_constant(Value value, ValueKind kind);
    void emit_string(const std::string& value);
    size_t emit_jump(OpCode op);
    void patch_jump(size_t operand_offset);
    void emit_loop(size_t loop_start);
    void emit_default(const Type* type);
    void emit_pops(int count);
    void emit_print(const Type* type, const SourceSector& sector);

    // ---- scopes ----
    void begin_scope();
    void end_scope();
    void declare_local(const std::string& name, const Type* type,
                       const SourceSector& sector);
    int resolve_local(const std::string& name) const;
    int locals_above(int depth) const;

    // ---- expressions ----
    const Type* compile_expr(Expr* expr, const Type* expected = nullptr);
    const Type* probe_type(Expr* expr);
    void compile_stmt(Stmt* stmt);
    bool is_assignable(const Type* from, const Type* to) const;
    void coerce(const Type* from, const Type* to, const SourceSector& sector,
                const std::string& context);
    void expect_bool(const Type* type, const SourceSector& sector,
                     const std::string& context);
    const Type* emit_binary_op(BinaryExpr::Op op, const Type* left, const Type* right,
                               const SourceSector& sector);
    void compile_args(const std::vector<ExprPtr>& args,
                      const std::vector<const Type*>& params, size_t first_param,
                      const SourceSector& sector, const std::string& callee);
    const Type* emit_call(FunctionInfo* function, const std::vector<ExprPtr>& args,
                          size_t first_param, const SourceSector& sector);
    const Type* compile_construction(const Type* type, const std::vector<ExprPtr>& args,
                                     const SourceSector& sector);
    const Type* compile_method_call(GetExpr* callee, CallExpr* call);
    const Type* compile_template_call(FuncDecl* decl, CallExpr* call);
    void unify(const TypeExpr& param, const Type* actual,
               const std::vector<std::string>& names, TypeSubstitution& bound);
    LValue compile_lvalue_prefix(Expr* target);
    const Type* compile_assignment(Expr* target, std::optional<BinaryExpr::Op> op,
                                   const std::function<const Type*(const Type*)>& rhs,
                                   const SourceSector& sector);

    // ---- statements ----
    bool always_returns(Stmt* stmt) const;
    bool contains_break(Stmt* stmt) const;

  public:
    void visitInt(IntExpr* expr) override;
    void visitFloat(FloatExpr* expr) override;
    void visitBool(BoolExpr* expr) override;
    void visitString(StringExpr* expr) override;
    void visitNull(NullExpr* expr) override;
    void visitThis(ThisExpr* expr) override;
    void visitVar(VarExpr* expr) override;
    void visitUnary(UnaryExpr* expr) override;
    void visitBinary(BinaryExpr* expr) override;
    void visitAssign(AssignExpr* expr) override;
    void visitIncrement(IncrementExpr* expr) override;
    void visitCall(CallExpr* expr) override;
    void visitGet(GetExpr* expr) override;
    void visitIndex(IndexExpr* expr) override;
    void visitCast(CastExpr* expr) override;
    void visitVector(VectorExpr* expr) override;

    void visitExprStmt(ExprStmt* stmt) override;
    void visitPrint(PrintStmt* stmt) override;
    void visitVarStmt(VarStmt* stmt) override;
    void visitBlock(BlockStmt* stmt) override;
    void visitIf(IfStmt* stmt) override;
    void visitWhile(WhileStmt* stmt) override;
    void visitFor(ForStmt* stmt) override;
    void visitReturn(ReturnStmt* stmt) override;
    void visitBreak(BreakStmt* stmt) override;
    void visitContinue(ContinueStmt* stmt) override;
    void visitFuncDecl(FuncDecl* stmt) override;
    void visitClassDecl(ClassDecl* stmt) override;
};
