# Lix

Lix is a statically typed, C++-flavoured language based on **Lox** from Robert Nystrom's
[*Crafting Interpreters*](https://craftinginterpreters.com) (Lox + feLIX = Lix).
It has a hand-written scanner, a recursive descent parser, a single-pass type-checking
compiler that emits typed stack bytecode, and a stack VM with a precise mark-and-sweep
garbage collector.

```cpp
template <typename T>
class Stack
{
    vector<T> items;
    void push(T item) { items.push(item); }
    T pop() { return items.pop(); }
    bool empty() { return items.size() == 0; }
}

class Point
{
    int x;
    int y;
    Point(int x, int y) { this.x = x; this.y = y; }
}

Stack<vector<Point>> paths = Stack<vector<Point>>();
paths.push([Point(0, 0), Point(1, 2)]);
print paths.pop()[1];   // Point{x: 1, y: 2}
```

## Building and running

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/lix examples/tour.lix   # run a file
./build/lix                     # start the REPL
```

If GNU readline is installed, the REPL uses it for line editing and history. Turn it off
with `-DLIX_USE_READLINE=OFF`.

Exit codes: `65` compile error, `70` runtime error (like clox).

### The REPL

```
$ lix
Lix REPL. Type :help for help, :quit or Ctrl-D to exit.
lix> int square(int n)
...> {
...>     return n * n;
...> }
lix> square(12)
144
lix> auto points = [1.5, 2]
lix> points
[1.5, 2.0]
```

- Classes, functions and globals stay available in later entries, and so do the values of
  the globals.
- A top-level expression prints its value. A missing `;` at the end is added.
- An entry continues while it is incomplete: open brackets, or a header such as
  `int f()` or `for (...)` still waiting for its body. An empty line ends an entry
  early. After an `if` without `else`, the REPL waits for one more line in case it
  starts with `else`.
- An entry with a compile error is rolled back completely, so it leaves no
  half-declared function or class behind. Runtime errors end only the current entry.
- Line numbers in messages count across the whole session.

### Looking inside: debug output for every stage

Each stage of the pipeline can show what it produced. All options work for files and in
the REPL, where each one can also be toggled with the command of the same name.

| Option        | REPL         | Shows                                                         |
|---------------|--------------|---------------------------------------------------------------|
| `--tokens`    | `:tokens`    | every token from the scanner, with line:column                |
| `--ast`       | `:ast`       | the syntax tree from the parser                               |
| `--disasm`    | `:disasm`    | the bytecode of every function the compiler produced (in the REPL: what *this* entry compiled) |
| `--trace`     | `:trace`     | before every instruction: the stack of the current frame with typed values, then the instruction |
| `--stats`     | `:stats`     | time per stage, functions and bytes compiled, instructions executed, calls, max stack and call depth, allocations, GC runs, peak heap, and an opcode histogram |
| `--gc-stress` | `:gc-stress` | collect garbage before *every* allocation                     |
| `--no-run`    |              | stop after compiling                                          |

The dumps (`--tokens`, `--ast`, `--disasm`) go to stdout so they can be redirected into a
file. `--trace` and `--stats` go to stderr because they interleave with the program's own
output.

```
$ lix --trace half.lix               # float half(int x) { return x / 2.0; }
  [<script>] stack: [ 5 ]
  0003    | CALL                1 (half)
    [half] stack: [ 5 ] [ 5 ] [ 2.0 ]
    0005    | INT_TO_FLOAT_UNDER
    [half] stack: [ 5 ] [ 5.0 ] [ 2.0 ]
    0006    | DIV_FLOAT
```

Runtime errors print a stack trace, innermost frame first, with the source line and the
bytecode offset and instruction of every frame. Deep recursion is shortened to its first
and last frames:

```
Runtime error: Vector index 5 out of range (size 0).
  at inner (line 1, 0008 VEC_SET)
  at outer (line 2, 0002 CALL)
  at <script> (line 3, 0000 CALL)
```

### Tests

```bash
python3 tests/run_tests.py build/lix              # all tests
python3 tests/run_tests.py build/lix --gc-stress  # again, collecting on every allocation
python3 tests/run_tests.py build/lix templates    # only tests whose path contains "templates"
```

Or run `ctest --test-dir build`, which does both. A test is a `.lix` file that states its
expected result in comments:

```cpp
print 1 + 2;       // expect: 3
int x = "a";       // expect error: Cannot convert 'string' to 'int'
print v[10];       // expect runtime error: out of range
```

For debugging the implementation itself, configure with `-DLIX_SANITIZE=ON
-DCMAKE_BUILD_TYPE=Debug` to build with AddressSanitizer and UBSan.

## The language

### Types

| Type            | Notes                                                                    |
|-----------------|--------------------------------------------------------------------------|
| `int`           | 64-bit signed, wraps on overflow                                         |
| `float`         | 64-bit double                                                            |
| `bool`          | `true` / `false`                                                         |
| `string`        | immutable; `+` concatenates, `==`, `<` etc. compare contents, `s.length()` |
| `vector<T>`     | growable array, any `T` including other vectors and classes              |
| classes         | user defined, optionally templates                                       |
| `auto`          | for variables: takes the type of the initializer                         |

Types nest freely: `vector<vector<Point>>`, `Box<vector<int>>`, `Pair<string, Box<float>>`.

**Conversions.** `int` converts to `float` implicitly. Everything else needs an explicit
`cast<T>(value)`: `float` to `int` (truncates), `int` and `bool` back and forth, and any
primitive to `string`. Conditions must be `bool`, so `if (count)` is an error. Write
`if (count != 0)`.

**Reference semantics.** Strings, vectors and class instances are heap objects. Assigning
them or passing them to a function shares the object, like in Java or C#, so a function
can modify the vector you pass in. `==` on classes and vectors compares identity.

**Default values.** A variable or field without an initializer starts as `0`, `0.0`,
`false`, `""`, an empty vector, or `null` for class types. Only class types can be `null`.
Vectors and strings never are.

### Statements

```cpp
int x = 1;  auto y = 2.5;  vector<int> v;      // declarations
x = 5;  x += 2;  x++;  --x;                     // assignment (also on fields and v[i])
print expr;                                     // print any value, including vectors and objects
if (cond) ... else ...
while (cond) ...
for (int i = 0; i < n; i++) ...                 // break and continue work in both loops
{ ... }                                         // block with its own scope
return expr;
```

Operators: `+ - * / %`, `== != < <= > >=`, `&& ||` (also spelled `and` / `or`), `!`, unary `-`.

### Functions

```cpp
int fib(int n)
{
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
```

Functions and classes can be used before their declaration. Top-level variables are
globals that every function can use. The compiler checks that every path of a non-void
function returns a value.

### Classes

```cpp
class Account
{
    string owner;
    int balance = 100;            // field initializer

    Account(string owner)         // constructor (at most one; optional)
    {
        this.owner = owner;       // 'this.' is optional when nothing shadows the field
    }

    void deposit(int amount) { balance += amount; }
}

Account a = Account("felix");
a.deposit(5);
print a;                          // Account{owner: "felix", balance: 105}
```

Field initializers run at the start of the constructor. A class without a constructor
gets one with no parameters. Calling a method on, or reading a field of, `null` is a
runtime error.

### Vectors

```cpp
vector<int> v = [1, 2, 3];        // vector literal; element type inferred or taken from the context
vector<float> f = [1, 2.5];       // elements convert to the declared element type
auto empty = vector<string>();    // empty vector of an explicit type
v.push(4);  v.pop();  v.size();  v.clear();
v[0] = 10;  v[1] += 1;
```

Indexing out of range or popping an empty vector is a runtime error.

### Templates

Classes and functions can be templates, with C++ syntax:

```cpp
template <typename K, typename V>
class Pair
{
    K key;
    V value;
    Pair(K key, V value) { this.key = key; this.value = value; }
}

template <typename T>
T max(T a, T b) { if (a > b) return a; return b; }

max(1, 2);             // T = int, inferred from the arguments
max<float>(1, 2.5);    // T given explicitly
Pair<string, int>("a", 1);
```

- Template arguments of function calls are inferred where possible, including from
  nested types such as `vector<T>` and `Box<T>`.
- Inside a class template, its bare name means the current instantiation. Inside
  `Tree<T>`, `Tree left;` means `Tree<T> left;`.
- `T()` is the default value of `T`: `0` for `int`, an empty vector for `vector<int>`,
  and so on.
- Like C++, templates are type-checked per instantiation (*monomorphization*), and a
  member of a class template is only compiled when it is used. A `Bag<string>` is fine
  even if `Bag` has a `sum()` that only works for numbers, as long as nobody calls it.

### Not (yet) supported

Inheritance and virtual methods, function overloading, operator overloading, first-class
functions and closures, `>>`-style shift operators, value semantics for classes, modules,
and native functions such as `sqrt` or file I/O. See *Ideas for next steps* below.

## How it works

```
 source ──► Scanner ──► tokens ──► Parser ──► AST ──► Compiler ──► Program ──► Vm
            (frontend/)            (frontend/)  (ast/)   (compiler/,   (bytecode/)  (vm/)
                                                         types/)
```

| Directory       | Contents                                                                 |
|-----------------|--------------------------------------------------------------------------|
| `src/common/`   | `SourceSector` (positions) and `Diagnostics` (error messages with carets) |
| `src/frontend/` | `Scanner` and `Parser`                                                   |
| `src/ast/`      | `Expr`, `Stmt` and `TypeExpr` nodes with visitors; `AstPrinter`          |
| `src/types/`    | `Type`, `VectorType`, `ClassType`, `FunctionInfo` and the interning `TypeTable` |
| `src/compiler/` | `Compiler`: type checking, template instantiation, code generation       |
| `src/bytecode/` | `OpCode` list, `Chunk`, `Program`, disassembler                          |
| `src/vm/`       | `Vm` interpreter loop, heap objects, garbage collector, trace and stats  |
| `src/driver/`   | `Pipeline`: runs all stages with their debug output, shared by file mode and REPL |
| `src/repl/`     | `Repl`: reading (multi-line) entries, commands                            |

### Why composite types failed in the old Lix, and what changed

The old implementation (`lix_old/Lix`) represented types as a flat enum:

```cpp
enum class LixDatatype : uint8_t { LD_Byte, LD_ShortInt, LD_Int, LD_Float, LD_Double, LD_LongInt, LD_Bool, LD_FUNC };
```

and picked instructions with arithmetic on that enum:

```cpp
currentChunk()->write((uint8_t)Chunk::OpCode::OP_ADD_BYTE + (uint8_t)resultingType);
```

An enum can only name a fixed set of types. `vector<double>` needs a type that *contains*
another type, `vector<vector<MyClass>>` needs that recursively, and `MyClass` needs types
that are created while compiling. No enum can hold those. On top of that, the VM used a
**byte stack**: an `int` took 4 bytes and a `bool` 1, so every instruction, every
variable offset and every `pop` had to know the exact size of every type. That gets very
hard once types can be arbitrarily large structures.

The rewrite fixes this with four ideas:

1. **Two kinds of types.** `TypeExpr` (in `ast/`) is a type *as written*, such as
   `vector<T>`. The parser doesn't know what the names mean. `Type` (in `types/`) is a
   *resolved* type, and `VectorType` points to its element type, so types nest to any
   depth. The compiler turns one into the other with `resolve_type`.

2. **Interning.** The `TypeTable` creates each type exactly once, so there is a single
   `vector<int>` object, and two types are equal exactly when their pointers are equal.
   Type checking is mostly pointer comparisons.

3. **Uniform 8-byte slots.** Every value is a `union Value { int64_t i; double f; bool b;
   Obj* o; }`. A `vector<Point>` is just a pointer to a heap `ObjVector`, so it's as easy
   to store, copy and pass as an `int`. Locals are numbered by slot, not byte offset.

4. **Typed bytecode, untyped VM.** Instructions are still typed like the original design
   (`ADD_INT`, `ADD_FLOAT`, `CONCAT`, `EQ_STRING`, `EQ_REF`, ...), and the compiler picks
   them from the static types, so the VM never checks a type at runtime. The VM only knows
   whether a slot holds a reference (`ValueKind::REF`), which the garbage collector needs.

### The compiler and templates

`Compiler` type-checks and emits code in **one pass**. Every `visitXxx` for an expression
emits that expression's code and records its static type in `result_type`, much like the
old compiler's `m_Stack` of types, but with real `Type*` objects.

That makes templates nearly free. To instantiate `Box<int>`, the compiler creates a
`ClassType` named `Box<int>` with the substitution `T = int`, then compiles the same AST
of `Box`'s methods with that substitution. `Box<float>` is a separate `ClassType` whose
methods compile to `ADD_FLOAT` where `Box<int>` uses `ADD_INT`. This is the same
monomorphization C++ does.

Compilation order (see the comment in `Compiler.hpp`):

1. Collect all top-level classes and functions by name, so everything can be used before
   its declaration.
2. Instantiate non-template classes, declare non-template functions and globals. Their
   bodies go into a `pending` queue.
3. Compile the top-level statements into the `<script>` function.
4. Drain the queue. Compiling a body can instantiate new templates, which queues more
   bodies, until everything reachable has been compiled.

Self-referencing types like `class Node { Node next; }` work because a class is put into
the type cache *before* its fields are resolved. Infinitely expanding templates such as
`Grow<T> { Grow<vector<T>> next; }` are caught by an instantiation depth limit.

### Parsing ambiguities

`Box<int> b;` and `a < b;` start the same way. The scanner tokenizes the whole file up
front, so the parser can look ahead and backtrack:

- A statement is a declaration if a type followed by a name can be parsed
  (`looks_like_declaration`).
- After an identifier, `<types>` followed by `(` is a template call or construction
  (`try_call_type_args`). Otherwise the `<` is a comparison.
- There is no `>>` token, so `vector<vector<int>>` needs no special case.

As in C#, an expression like `f(a < b, c > (d))` is read as a template call. Add
parentheses if you mean two comparisons.

### The VM and garbage collector

The VM is a classic loop over a `switch` with a value stack and call frames. A call
pushes a frame whose locals start at the first argument's slot. Methods get `this` in
slot 0.

The collector is a **precise** mark-and-sweep. Next to the value stack is a parallel
array with the `ValueKind` (int, float, bool or reference) of every slot. Because the
instructions are typed, every instruction knows what kind of value it pushes, so the
array is always right. The GC uses it to find references, and `--trace` uses it to print
values with their real types. Instances
know which fields are references from their `ClassInfo`, and vectors store their
element kind. Run any program with `--gc-stress` to collect before every single
allocation. The whole test suite passes in that mode.

## Where optimizations fit in

The `Pipeline` (`src/driver/Pipeline.cpp`) runs scan, parse, compile and run in order,
so each level of optimization has an obvious place:

- **On the AST**, between `parser.parse()` and `compiler.compile()`: constant folding
  (`2 * 3`), dead branches (`if (false)`), algebraic simplification. Write it as another
  `VisitorExpr`/`VisitorStmt`, like `AstPrinter`. Keep in mind that types are only known
  inside the compiler. Folding `1 + 2.5` correctly needs the int-to-float promotion, so
  type-dependent rewrites are easier inside `Compiler::visitBinary`.
- **On the bytecode**, after `compiler.compile()`, per `Function`: peephole rules
  (`CONST 1; INT_TO_FLOAT` → `CONST 1.0`, `NOT; JUMP_IF_FALSE` → `JUMP_IF_TRUE`),
  superinstructions for frequent pairs (`--stats` shows the opcode histogram), or
  dropping a `POP` after a `SET_LOCAL`. Jump offsets are relative, so removing bytes
  means re-patching the jumps around them. The `--disasm` output before and after is
  the quickest check.
- **In the VM**: computed-goto dispatch instead of `switch`, caching `ip` and `sp` in
  registers, avoiding the `stack_kinds` writes when no GC can happen, and so on.
  `--stats` counts instructions and `run` time, and `--gc-stress` plus the test suite
  catch mistakes in GC-related changes.

Run `python3 tests/run_tests.py build/lix` after every change; the 65 tests cover
every instruction.

## Ideas for next steps

- **Inheritance and virtual methods**: add a superclass to `ClassType`, copy field
  layouts, and add a vtable index and a `CALL_VIRTUAL` instruction.
- **Native functions** (`sqrt`, `clock`, `read_line`): a `CALL_NATIVE` instruction and a
  table of C++ functions with Lix signatures.
- **Function overloading**: pick among candidates by argument types in `visitCall`.
- **Interfaces / constraints for templates**, for errors at the call site instead of
  inside the template body.
- **Value-type structs**: a `struct` kind that is copied on assignment. Needs multi-slot
  values, which the slot design makes manageable.
- **Lambdas and closures**: a function type in `types/`, plus upvalues as in clox.
- **A real `main`**: call `int main()` if it exists and use its return value as the exit code.

## History

The earlier attempts are kept in `lix_old/`:

- `jLox`, `lox-c`: implementations of the book's tree-walking and bytecode interpreters.
- `Lix` (2020–2021): the first statically typed attempt, with a byte-addressed stack VM.
- `Lyx` (2021–2025): a parser rewrite with source sectors and AST pretty-printing.
- `Lix2` (2025): Lyx with a `Datatype` class hierarchy, without a compiler yet.

This version keeps the original pipeline (hand-written scanner, recursive descent parser,
compiler to custom typed stack bytecode, stack VM), the `SourceSector` idea from Lyx and
Lix2, `cast<T>(...)`, the visitor-based AST and the `.clang-format` style.
