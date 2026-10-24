# SPScript — Architecture

A detailed guide to the internals of **Spectra3D Script (SPScript)**: a
bytecode-compiled, stack-based scripting language designed to be embedded
in game engines.

---

## 1. Overview — the pipeline

```
  ┌─────────────┐
  │  source.sps │
  └──────┬──────┘
         │ text
         ▼
  ┌──────────────┐  tokens   ┌──────────────┐   AST    ┌────────────────┐
  │   Lexer      │ ────────► │   Parser     │ ───────► │   Compiler     │
  │  (lexer.c)   │           │  (parser.c)  │          │  (compiler.c)  │
  └──────────────┘           └──────────────┘          └───────┬────────┘
                                                               │  Program
                                                               │  (bytecode +
                                                               │   constants +
                                                               │   class protos)
                                                               ▼
                                                      ┌────────────────┐
                                                      │   VM           │
                                                      │   (vm.c)       │
                                                      └────────────────┘
                                                               │
                                             optional persist  ▼
                                                      ┌────────────────┐
                                                      │  program.c     │
                                                      │  (.spc file)   │
                                                      └────────────────┘
```

Each stage is independent: the compiler writes a `Program` struct, which the
VM consumes. The `Program` can be serialized to a `.spc` bytecode file and
re-loaded without re-compiling.

---

## 2. Directory structure

```
Spectra3D_Scripts/
├── src/                    # All C implementation files
│   ├── api/                # Public C API implementation
│   ├── arena.c             # Arena allocator
│   ├── builtins.c          # Built-in function table
│   ├── compiler.c          # AST → bytecode compiler
│   ├── dict.c              # Hash map (open addressing)
│   ├── lexer.c             # Tokenizer
│   ├── main.c              # spsc entry point (compiler CLI)
│   ├── methods.c           # Built-in type methods (string, list, dict)
│   ├── object.c            # Heap objects (strings, lists, closures, classes)
│   ├── parser.c            # Recursive descent parser → AST
│   ├── program.c           # Program container + .spc serialization + OP_NAMES
│   ├── regex.c             # Regular expression engine (pure C11)
│   ├── spl.c               # SPL format parser and writer (C)
│   ├── spslib.c            # Standard library (files, time, JSON, math, colors)
│   ├── term.c              # ANSI terminal color helpers
│   ├── vm.c                # Bytecode interpreter (computed goto)
│   └── vm_main.c           # spsvm entry point (VM-only runner)
│
├── include/
│   ├── internal/           # Internal headers (moved from src/ — not for host use)
│   │   ├── arena.h
│   │   ├── ast.h
│   │   ├── builtins.h
│   │   ├── compiler.h
│   │   ├── dict.h
│   │   ├── lexer.h
│   │   ├── methods.h
│   │   ├── parser.h
│   │   ├── program.h       # OpCode enum + OP_NAMES[] + Program struct
│   │   ├── regex.h
│   │   ├── spl.h
│   │   ├── spslib.h
│   │   ├── term.h
│   │   ├── value.h
│   │   ├── vm.h
│   │   └── api_internal.h  # Internal bridge between core and public API
│   └── sps/                # Public API headers (for host applications)
│       ├── sps.h           # Main umbrella header
│       ├── sps_types.h
│       ├── sps_value.h
│       ├── sps_context.h
│       ├── sps_function.h
│       ├── sps_class.h
│       └── sps_compiler.h
│
├── include/spl/
│   └── spl.hpp             # Header-only SPL parser/writer for C++ hosts
│
├── examples/               # Example scripts and host embedding demo
├── tests/                  # Test scripts and SPL compatibility test
├── docs/                   # Extended API reference
├── bin/                    # Build output (spsc.exe, spsvm.exe, libsps.a)
├── out/                    # Compiled .spc bytecode output
├── build.bat               # Windows build (GCC / MSYS2)
└── Makefile                # MSYS2 make build
```

> [!NOTE]
> All `#include "internal/X.h"` directives are resolved by the `-Iinclude`
> compiler flag. The `-Isrc` flag is kept for `.c` file locality but no
> headers remain in `src/` directly.

---

## 3. Source files — `src/`

### Core language files

| File | Description |
|------|-------------|
| `arena.c` | Block-based arena allocator used by the compiler. All AST nodes, names, and function prototypes are allocated from this arena; a single `arena_release()` reclaims everything. |
| `lexer.c` | Hand-written tokenizer. Reads UTF-8 source text and produces a flat token stream. Tracks line numbers for error reporting. |
| `parser.c` | Recursive descent parser. Consumes tokens and builds an AST (`ast.h`). Handles operator precedence via Pratt parsing. |
| `compiler.c` | The core of the language. Walks the AST and emits `Instr` bytecode into `FunctionProto` objects. Manages local slots, upvalue capture, and constant folding. Performs peephole optimizations (e.g. fusing `GET_LOCAL + CONST` into `GET_LOCAL_CONST`). |
| `vm.c` | Stack-based bytecode interpreter. Uses GCC computed goto (`&&label`) for a direct-threaded dispatch loop. Manages call frames, upvalues, error handlers (`try/catch`), and host class dispatch. |
| `object.c` | Allocates and manages heap objects: `ObjString`, `ObjList`, `ObjClosure`, `ObjObject` (class instances), `ObjIter`. All live objects are chained on `vm->objects` and freed when the VM is released. |
| `dict.c` | Open-addressing hash map with Robin Hood hashing. Used for script dictionaries (`dict` type) and class field lookup. |
| `methods.c` | Implements built-in methods on `string`, `list`, and `dict` values (`upper()`, `split()`, `append()`, etc.). |
| `program.c` | Owns the `Program` container (prototypes, class protos, global name table). Hosts `OP_NAMES[]` (moved from former `opcode.c`) and the binary `.spc` serializer/deserializer. |
| `builtins.c` | Defines the `SPS_BUILTINS[]` table — the mapping of built-in function names to C function pointers. |
| `spslib.c` | Standard library: file I/O, time, JSON, SPL format bridge, collection utilities (`zip`, `map`, `filter`, …), math, regex wrappers, `Result` type, and colored print functions. |
| `regex.c` | Pure C11 regex engine. Compiles patterns to an internal bytecode and matches with an explicit stack (no recursion → no stack overflow on long input). |
| `spl.c` | SPL (Spectra3D data format) parser and writer. Interoperable with `include/spl/spl.hpp`. |
| `term.c` | ANSI escape code helpers. `term_color()` and `term_reset()` are no-ops when stdout is not a TTY (piped output stays clean). |
| `main.c` | Entry point for `spsc.exe` (the compiler CLI). Handles `--run`, `--dump`, `--repl`, `--stats`, `--time`, `-o`. |
| `vm_main.c` | Entry point for `spsvm.exe` (the standalone VM runner). Loads a `.spc` file and runs it, or `--dump` prints the disassembly. |

### API files — `src/api/`

| File | Description |
|------|-------------|
| `api_context.c` | `SpsContext` lifecycle, error handler, global variable access, script execution. |
| `api_value.c` | Value construction/inspection (`sps_value_int`, `sps_value_as_string`, …) and SPL ↔ `SpsValue` conversion. |
| `api_function.c` | Registering host native functions (`sps_register_function`) and calling script functions from the host (`sps_call`). |
| `api_class.c` | Registering host classes with constructors, methods, and properties (`sps_host_class_new`, etc.). |
| `api_compiler.c` | Compiling source files and strings to `SpsChunk`; running chunks; exposing compiler diagnostics. |
| `api_version.c` | Returns the version string `sps_version()`. |

---

## 4. Internal headers — `include/internal/`

> [!IMPORTANT]
> These headers are **not** part of the public API. Host applications must
> not include them directly. Use `include/sps/sps.h` instead.

| Header | Description |
|--------|-------------|
| `arena.h` | `Arena` type and allocation functions. |
| `ast.h` | All AST node types produced by the parser. |
| `builtins.h` | `BuiltinDef` struct and `SPS_BUILTINS[]` / `SPS_BUILTIN_COUNT` extern declarations. |
| `compiler.h` | `sps_compile()` and related types. |
| `dict.h` | `ObjDict` and hash map API. |
| `lexer.h` | `Lexer`, `Token`, and `TokenType` definitions. |
| `methods.h` | Method group indices and the `vm_method_call()` dispatcher. |
| `parser.h` | `sps_parse()` and `ParseResult`. |
| `program.h` | **`OpCode` enum**, `OP_NAMES[]` extern, `Instr`, `FunctionProto`, `ClassProto`, `Program`. |
| `regex.h` | `Regex` type and the regex API used by `spslib.c`. |
| `spl.h` | Internal SPL parser/writer API used by `spl.c` and `spslib.c`. |
| `spslib.h` | Declarations for all standard library functions including the color print functions. |
| `term.h` | ANSI escape code macros (`TERM_RED`, …) and `term_color()` / `term_reset()`. |
| `value.h` | `Value` tagged union, `ValueType` enum, `ObjString`, `ObjList`, `ObjClosure`, `NativeFn`, etc. |
| `vm.h` | `VM` struct, `CallFrame`, `ErrorHandler`, and all VM API functions (`vm_new`, `vm_run`, `vm_runtime_error`, …). |
| `api_internal.h` | Internal bridge types: `SpsBinding`, `SpsProperty`, `SpsContext` (full definition), `SpsChunk` (full definition). Only used inside `src/api/`. |

---

## 5. Public API headers — `include/sps/`

| Header | What it exposes |
|--------|----------------|
| `sps.h` | Umbrella header — includes all others. The only header a host needs. Defines `SPS_VERSION_MAJOR/MINOR/PATCH` and `sps_version()`. |
| `sps_types.h` | Opaque handle types (`SpsContext*`, `SpsValue`, `SpsChunk*`, `SpsHostClass*`) and callback typedefs (`SpsNativeFn`, `SpsErrorHandler`, `SpsPropertyGetter`, `SpsPropertySetter`). |
| `sps_value.h` | Value constructors (`sps_value_nil`, `sps_value_int`, …) and type predicates / accessors (`sps_value_is_int`, `sps_value_as_string`, …). |
| `sps_context.h` | Context lifecycle, error handling, global variables, and script execution (`sps_context_new`, `sps_context_exec_file`, `sps_context_get_global`, …). |
| `sps_function.h` | Registering native C functions and calling script functions from the host (`sps_register_function`, `sps_call`). |
| `sps_class.h` | Registering host (C/C++) classes with constructors, methods and property getters/setters (`sps_host_class_new`, `sps_host_class_method`, `sps_host_class_property`, …). |
| `sps_compiler.h` | Compiling source files to bytecode chunks and accessing compiler diagnostics (`sps_compile_file`, `sps_compile_string`, `sps_diagnostic_count`, …). |

### C++ (engine) header

| Header | Description |
|--------|-------------|
| `include/spl/spl.hpp` | Header-only C++17 SPL parser and writer. Replaces `json.hpp` for the engine's data format. Interoperable with `src/spl.c` (verified by the compat test). |

---

## 6. Memory model

SPScript uses a **no-GC, manual lifetime** strategy optimized for game engine
use (no stop-the-world pauses).

### Compiler arena

The lexer, parser, and compiler allocate **all** temporary data (tokens, AST
nodes, names, upvalue descriptors, prototype arrays) from a single `Arena`.
When compilation finishes, `arena_release()` frees everything with a single
`free()` loop over the arena's blocks — zero individual frees, zero leaks.

### VM heap objects

Every heap-allocated runtime object (`ObjString`, `ObjList`, `ObjClosure`,
`ObjObject`, …) is appended to `vm->objects` on creation. When `vm_free()` is
called (when the script is done or the context is destroyed) the list is walked
once and every object is freed. There is no garbage collector and no GC pauses.

### Frame arena

Local variable slots for **all active call frames** live in a single flat
buffer `vm->frame_arena` of size `VM_FRAME_ARENA_MAX` (256 KiB). Each
`CallFrame` records its `slot_base` offset into this buffer. On function
return the arena top is restored to `slot_base` in a single assignment —
no per-slot cleanup, no zeroing. The compiler always writes a slot before
reading it, so uninitialized data is never observed.

### Operand stack

The operand stack (`vm->stack`, `VM_STACK_MAX` = 65536 `Value` slots) is a
flat array. `vm->stack_top` is the index of the next free slot.

```
Memory layout during execution
───────────────────────────────────────────────────────────────────
 vm->stack        [  operand stack  ]  (64 KB of Value)
 vm->frame_arena  [  local slots of all active frames  ]  (256 KB)
 vm->objects →    linked list of heap objects
 vm->globals      [  global variable array  ]
```

---

## 7. Bytecode format — `.spc` files

The binary format written by `program_serialize()` and read by
`program_deserialize()`:

```
"SPSB"                    4 bytes — magic
u32 version               1 (SPS_FILE_VERSION)
u32 proto_count
for each FunctionProto:
    u32 name_len
    u8[name_len] name
    i32 arity
    i32 upvalue_count
    i32 stack_size        (max local slots needed)
    u32 code_len
    for each Instr:
        u8  op            (OpCode)
        u8  a
        u8  b
        u8  _pad
        i32 c
        i32 line
    u32 const_count
    for each constant:
        u8 tag            (0=nil 1=bool 2=int 3=float 4=string)
        ... tag-specific payload
    u32 upvalue_count
    for each upvalue:
        i32 index
        u8  is_local
u32 class_count
for each ClassProto:
    u32 name_len, u8[name_len] name
    u8  is_struct
    u32 field_count, fields...
    u32 method_count, methods (name + proto index)...
    i32 init_proto
u32 global_count
for each global name:
    u32 len, u8[len] name
u32 entry_proto           index of the top-level function
```

All integers are **little-endian**. Doubles are serialized as their 64-bit
IEEE 754 bit pattern.

---

## 8. The VM dispatch loop

`vm_execute()` in `src/vm.c` is the hot loop. It uses GCC's **computed goto**
extension for direct-threaded dispatch:

```c
// One label per opcode in a static table
static void* dispatch_table[] = { &&do_OP_CONST, &&do_OP_NIL, ... };

// Dispatch macro
#define DISPATCH() goto *dispatch_table[ip->op]

// Fetch macro
#define READ_INSTR() (ip++)

DISPATCH();   // jump to the first instruction

do_OP_CONST: {
    // ... push constant
    DISPATCH();
}
do_OP_ADD: {
    // ... pop two values, push sum
    DISPATCH();
}
// ...
```

This avoids the branch predictor mis-predictions of a `switch` statement
and gives roughly **20% throughput improvement** over the switch-based
equivalent.

### Hot-path optimizations

| Technique | Effect |
|-----------|--------|
| Computed goto (direct threading) | One indirect jump per instruction, no branch prediction penalty |
| Fused instructions (`GET_LOCAL_CONST`, `GET_GLOBAL_CONST`, `GET_LOCAL_ADD`, `ADD_CONST`) | Common consecutive pairs become a single instruction |
| Frame arena (no per-frame `malloc`) | Function calls/returns are single pointer increments |
| No GC | Zero GC pauses |
| Line number written only on change | No line number store on every instruction |
| Hot VM fields in local C variables | `ip`, `stack_top` stay in registers between instructions |

### Instruction encoding

Every instruction is a fixed-size `Instr` struct (12 bytes):

```c
typedef struct {
    uint8_t op;    // OpCode
    uint8_t a;     // first small operand (e.g. argument count)
    uint8_t b;     // second small operand (rarely used)
    uint8_t _pad;
    int32_t c;     // main operand (constant index, slot, jump target, …)
    int32_t line;  // source line (for stack traces)
} Instr;
```

---

## 9. OpCode reference

The `OpCode` enum and the matching `OP_NAMES[]` string table live in
`include/internal/program.h` and `src/program.c` respectively.

| OpCode | Operands | Stack effect | Description |
|--------|----------|--------------|-------------|
| `OP_CONST` | `c` = constant index | → push | Push constant from the pool |
| `OP_NIL` / `OP_TRUE` / `OP_FALSE` | — | → push | Push literal nil/true/false |
| `OP_POP` | — | pop → | Discard top of stack |
| `OP_DUP` | — | [v] → [v,v] | Duplicate top of stack |
| `OP_GET_LOCAL` | `c` = slot | → push | Push local variable |
| `OP_SET_LOCAL` | `c` = slot | pop → | Store top into local slot |
| `OP_GET_GLOBAL` | `c` = global index | → push | Push global variable |
| `OP_SET_GLOBAL` | `c` = global index | pop → | Store into global |
| `OP_GET_UPVAL` / `OP_SET_UPVAL` | `c` = upvalue index | … | Upvalue read/write |
| `OP_GET_LOCAL_CONST` | `a` = slot, `c` = const | → push×2 | Fused: local + constant |
| `OP_GET_GLOBAL_CONST` | `a` = global, `c` = const | → push×2 | Fused: global + constant |
| `OP_GET_LOCAL_ADD` | `a` = slot, `c` = const | → push | local + constant in one step |
| `OP_ADD_CONST` | `c` = const | [v] → [v+c] | Pop, add constant, push |
| `OP_ADD` / `OP_SUB` / `OP_MUL` / `OP_DIV` / `OP_MOD` | — | [a,b] → [r] | Arithmetic |
| `OP_NEG` / `OP_NOT` | — | [v] → [r] | Unary |
| `OP_EQ` / `OP_NE` / `OP_LT` / `OP_LE` / `OP_GT` / `OP_GE` | — | [a,b] → [bool] | Comparison |
| `OP_BITAND` / `OP_BITOR` / `OP_BITXOR` / `OP_SHL` / `OP_SHR` | — | [a,b] → [r] | Bitwise |
| `OP_JMP` | `c` = target | — | Unconditional jump |
| `OP_JMP_IF_FALSE` / `OP_JMP_IF_TRUE` | `c` = target | pop → | Conditional jump |
| `OP_CALL` | `a` = arg count | [fn, args…] → [result] | Call function/closure |
| `OP_METHOD` | `a` = argc, `c` = method group | [obj, args…] → [result] | Built-in method call |
| `OP_METHOD_NAME` | `a` = argc, `c` = name const | [obj, args…] → [result] | Script method call by name |
| `OP_RETURN` | — | [v] → | Return from function |
| `OP_CLOSURE` | `c` = proto index | → push | Create a closure |
| `OP_CLOSE_UPVAL` | `c` = slot threshold | — | Move upvalues to heap |
| `OP_NATIVE` | `c` = builtin index | → push native | Push a native function value |
| `OP_GET_FIELD` / `OP_SET_FIELD` | `c` = name const | [obj] → [v] | Object field access |
| `OP_GET_INDEX` / `OP_SET_INDEX` / `OP_GET_INDEX_KEEP` | — | [obj,idx] → [v] | Index operator |
| `OP_LEN` | — | [v] → [int] | Length of string/list/dict |
| `OP_IN` | — | [v,container] → [bool] | Membership test |
| `OP_LIST` | `a` = count | [v…] → [list] | Build a list literal |
| `OP_DICT` | `a` = pair count | [k,v…] → [dict] | Build a dict literal |
| `OP_ITER_INIT` | — | [iterable] → [iter] | Start iteration |
| `OP_ITER_NEXT` | `c` = end target | [iter] → [iter,v] | Advance iterator |
| `OP_CLASS` | `c` = class index | → push | Push a class object |
| `OP_CAST` | `c` = target type | [v] → [v] | Runtime type cast |
| `OP_FORMAT` | `c` = spec const (-1 = default) | [v] → [str] | Format a value |
| `OP_IMPORT` | `c` = module proto | — | Run a module |
| `OP_TRY` | `c` = handler offset | — | Register error handler |
| `OP_TRY_END` | — | — | Remove error handler |

---

## 10. Extension points — embedding the VM

### Minimal embedding

```c
#include "sps/sps.h"

SpsContext* ctx = sps_context_new();
sps_context_set_error_handler(ctx, my_error_handler, NULL);
sps_context_exec_file(ctx, "scripts/game.sps");
sps_context_free(ctx);
```

### Registering native (host) functions

The script calls C functions registered under a name:

```c
static SpsValue native_log(SpsContext* ctx, int argc, SpsValue* argv, void* user) {
    for (int i = 0; i < argc; i++)
        printf("%s ", sps_value_as_cstring(ctx, argv[i]));
    putchar('\n');
    return sps_value_nil();
}

sps_register_function(ctx, "log", native_log, NULL);
```

### Host classes (userdata objects)

The host can expose C structs to the script as first-class objects:

```c
SpsHostClass* cls = sps_host_class_new(ctx, "Entity");
sps_host_class_constructor(cls, entity_ctor);
sps_host_class_method(cls, "move",   entity_move);
sps_host_class_property(cls, "name", entity_get_name, entity_set_name, NULL);
```

The script then does `var e = Entity("player"); e.move(1, 0, 0); print(e.name)`.

### Calling script functions from the host

```c
SpsValue fn = sps_context_get_global(ctx, "on_update");
SpsValue delta = sps_value_float(0.016f);
sps_call(ctx, fn, 1, &delta);
```

### Error handler

```c
static void on_error(SpsContext* ctx, const char* message, void* user) {
    fprintf(stderr, "Script error: %s\n", message);
    // do NOT call sps_context_free here — the context is still valid
}
sps_context_set_error_handler(ctx, on_error, NULL);
```

Without an error handler the VM prints the error + stack trace to `stderr`
and calls `exit(1)`.

### Hot reload

```c
// Compile a new version of the script while the game is running
SpsChunk* chunk = sps_compile_file(ctx, "scripts/level.sps");
if (chunk) {
    sps_context_run(ctx, chunk);  // apply without restarting the host
    sps_chunk_free(chunk);
}
```

---

## 11. Build system

| Tool | File | Description |
|------|------|-------------|
| GCC (MSYS2/MinGW) | `build.bat` | Primary Windows build. Produces `bin/spsc.exe`, `bin/spsvm.exe`, `bin/libsps.a`. |
| make (MSYS2) | `Makefile` | Same targets, for `make` users. |

Compiler flags:
```
-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-clobbered
-static -Isrc -Iinclude
```

`-Isrc` is kept for `.c` file locality (so `#include "internal/..."` in
`src/api/` resolves via `-Iinclude`). All internal headers are under
`include/internal/`.

---

*Generated from the source tree — update when adding new files or opcodes.*
