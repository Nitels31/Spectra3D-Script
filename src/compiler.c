// Spectra3D Script - bytecode compiler (AST -> bytecode)
//
// Scope model:
//   - module level variables live in the global table
//   - function level variables live in local slots of the frame
//   - variables captured by nested functions become upvalues
#include "internal/compiler.h"
#include "internal/builtins.h"
#include "internal/methods.h"
#include "internal/parser.h"
#include "internal/term.h"

#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LOCALS    256
#define MAX_UPVALUES  64
#define MAX_SCOPES     64
#define MAX_LOOPS      32
#define MAX_BREAKS     64
#define MAX_FN_DEPTH   64

// the limits of the inlining of the calls
#define MAX_INLINE_PARAMS   4
#define MAX_INLINE_BODY     8
#define MAX_INLINE_DEPTH   16
#define MAX_INLINE_BLOCKS  64

// ------------------------------------------------------------------
// name map (hash map with open addressing)
// ------------------------------------------------------------------

typedef struct {
    const char* key;
    int   key_len;
    int   value;
    int   state; // 0 = empty, 1 = used
} MapEntry;

typedef struct {
    MapEntry* entries;
    int       capacity;
    int       count;
} Map;

static void* xmalloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    return ptr;
}

static void* xrealloc(void* ptr, size_t size) {
    void* result = realloc(ptr, size);
    if (!result) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    return result;
}

static void* xcalloc_helper(size_t size) {
    void* ptr = calloc(1, size);
    if (!ptr) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    return ptr;
}

static uint32_t map_hash(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (unsigned char)key[i];
        hash *= 16777619u;
    }
    return hash;
}

static void map_init(Map* map) {
    map->capacity = 256;
    map->count = 0;
    map->entries = (MapEntry*)xcalloc_helper(sizeof(MapEntry) * (size_t)map->capacity);
}

static void map_free(Map* map) {
    free(map->entries);
    map->entries = NULL;
    map->capacity = 0;
    map->count = 0;
}

static void map_insert(MapEntry* entries, int capacity, const char* key, int length, int value) {
    uint32_t index = map_hash(key, length) & (uint32_t)(capacity - 1);
    for (;;) {
        MapEntry* entry = &entries[index];
        if (entry->state == 0) {
            entry->key = key;
            entry->key_len = length;
            entry->value = value;
            entry->state = 1;
            return;
        }
        if (entry->key_len == length && memcmp(entry->key, key, (size_t)length) == 0) {
            entry->value = value;
            return;
        }
        index = (index + 1) & (uint32_t)(capacity - 1);
    }
}

static void map_grow(Map* map) {
    int capacity = map->capacity * 2;
    MapEntry* entries = (MapEntry*)xcalloc_helper(sizeof(MapEntry) * (size_t)capacity);
    for (int i = 0; i < map->capacity; i++) {
        if (map->entries[i].state == 1) {
            map_insert(entries, capacity, map->entries[i].key, map->entries[i].key_len,
                       map->entries[i].value);
        }
    }
    free(map->entries);
    map->entries = entries;
    map->capacity = capacity;
}

static int map_get(Map* map, const char* key, int length) {
    uint32_t index = map_hash(key, length) & (uint32_t)(map->capacity - 1);
    for (;;) {
        MapEntry* entry = &map->entries[index];
        if (entry->state == 0) return -1;
        if (entry->key_len == length && memcmp(entry->key, key, (size_t)length) == 0) {
            return entry->value;
        }
        index = (index + 1) & (uint32_t)(map->capacity - 1);
    }
}

static void map_put(Map* map, const char* key, int length, int value);

// Copies every name of a map into another one (the same values).
static void map_copy(Map* source, Map* dest) {
    for (int i = 0; i < source->capacity; i++) {
        if (source->entries[i].state == 1) {
            map_put(dest, source->entries[i].key, source->entries[i].key_len,
                    source->entries[i].value);
        }
    }
}

static void map_put(Map* map, const char* key, int length, int value) {
    if ((map->count + 1) * 4 >= map->capacity * 3) map_grow(map);
    uint32_t index = map_hash(key, length) & (uint32_t)(map->capacity - 1);
    for (;;) {
        MapEntry* entry = &map->entries[index];
        if (entry->state == 0) {
            entry->key = key;
            entry->key_len = length;
            entry->value = value;
            entry->state = 1;
            map->count++;
            return;
        }
        if (entry->key_len == length && memcmp(entry->key, key, (size_t)length) == 0) {
            entry->value = value;
            return;
        }
        index = (index + 1) & (uint32_t)(map->capacity - 1);
    }
}

// ------------------------------------------------------------------
// compiler state
// ------------------------------------------------------------------

typedef struct {
    int   index;
    bool  is_local;
    char* name;
    int   name_len;
} Upvalue;

typedef struct {
    char* name;
    int   name_len;
    int   depth;
    bool  captured;
} Local;

// A name of the language.
typedef struct {
    const char* name;
    int         name_len;
} NameRef;

// The information about a function of a module: the node of its "def", the
// globals that its body uses and the flags that decide if it can be inlined.
typedef struct {
    Node*    node;        // the node of the "def"
    NameRef* globals;     // the globals that the body uses
    int      global_count;
    bool     has_closure; // the body has a nested "def"
    bool     has_upvalue; // the body captures a variable
    bool     params_safe; // the identity of the parameters cannot be seen
    bool     can_fail;    // the body can raise an error that depends on the data
} FuncInfo;

typedef struct {
    int continue_target;
    int body_depth;
    bool has_iterator; // the loop keeps an iterator on the stack
    int breaks[MAX_BREAKS];
    int break_count;
} LoopCtx;

// The state of an inlined block: the jumps of the returns that are patched
// at the end of the block and the state of the stack when the block starts.
typedef struct {
    int* returns;      // the positions of the jumps of the returns
    int  return_count;
    int  return_cap;
    int  loop_base;    // the number of loops of the function before the block
    int  try_base;     // the number of error handlers before the block
    int  temp_slot;    // the local that keeps the value of a return
} InlineCtx;

typedef struct FunctionState {
    FunctionProto*        proto;
    Local                 locals[MAX_LOCALS];
    int                   local_count;
    Upvalue               upvalues[MAX_UPVALUES];
    int                   upvalue_count;
    int                   scope_depth;
    int                   scope_bases[MAX_SCOPES];
    bool                  scope_captured[MAX_SCOPES];
    LoopCtx               loops[MAX_LOOPS];
    int                   loop_count;
    InlineCtx             inlines[MAX_INLINE_DEPTH];
    int                   inline_count;
    int                   try_depth;      // the number of active error handlers
    NameRef*              free_globals;   // the globals that the body uses
    int                   free_global_count;
    int                   free_global_cap;
    bool                  collect_globals;
    struct FunctionState* enclosing;
    bool                  module_level; // the top level of a module
} FunctionState;

typedef enum { VAR_LOCAL, VAR_UPVAL, VAR_GLOBAL } VarKind;

typedef struct {
    VarKind kind;
    int     index;
} VarRef;

typedef struct {
    char* alias;     // the name used in "import ... as x"
    int   module;    // the index of the module
} AliasInfo;

// The information about a class during the compilation.
typedef struct {
    char*   name;
    bool    is_struct;
    int     field_count;
    char**  field_names;
    int     method_count;
    char**  method_names;
    int*    method_protos;
    Node**  defaults;      // the syntax tree of the default values of the fields
    int     default_count;
    Node*   init_node;     // the syntax tree of the constructor
    char*   parent;        // the name of the parent class
} ClassInfo;

typedef struct {
    char*     path;        // the file of the module
    char*     alias;       // the alias of the module itself
    Map       globals;     // name -> global index
    Map       functions;   // name -> the index of the function
    FuncInfo** function_list;
    int       function_count;
    int       function_cap;
    int       entry_proto;
    AliasInfo aliases[16];
    int       alias_count;
} ModuleInfo;

typedef struct Compiler {
    Arena*        arena;
    Program*      program;
    VM*           vm;
    FunctionState* fn;
    FunctionState  fn_stack[MAX_FN_DEPTH];
    int           fn_depth;
    ModuleInfo*   modules;
    int           module_count;
    int           module_cap;
    ModuleInfo*   current;  // the module that is being compiled
    ClassInfo**   classes;  // the classes of the compilation
    int           class_count;
    int           class_cap;
    Node*         inline_blocks[MAX_INLINE_BLOCKS]; // the functions that are not inlined
    int           inline_block_count;
    const NameRef* forced_globals;  // the globals of the function that is inlined
    int           forced_global_count;
    jmp_buf       error_jump;
    SpsCompileErrorHandler on_error;
    void*         error_user;
    char          file_name[512];
} Compiler;

// Adds the native functions of the vm to the globals of a module, so every
// module sees print(), len() and the functions of the host.
static void seed_module_globals(Compiler* compiler, ModuleInfo* module) {
    for (int i = 0; i < compiler->vm->native_count; i++) {
        const char* name = compiler->vm->natives[i].name;
        int length = (int)strlen(name);
        if (map_get(&module->globals, name, length) >= 0) continue;
        program_add_global_name(compiler->program, name, length);
        map_put(&module->globals, name, length, i);
    }
}

// ------------------------------------------------------------------
// the peephole pass
// ------------------------------------------------------------------

// The pairs of the instructions that are combined into one.
static bool can_combine(Instr* first, Instr* second) {
    if (first->op == OP_GET_LOCAL && second->op == OP_CONST) return true;
    if (first->op == OP_GET_GLOBAL && second->op == OP_CONST) return true;
    if (first->op == OP_CONST && second->op == OP_ADD) return true;
    return false;
}

// The triples of the instructions that are combined into one. The addition of
// a local and a constant is the most common sequence of the loops.
static bool can_combine_three(Instr* first, Instr* second, Instr* third) {
    if (first->op == OP_GET_LOCAL && second->op == OP_CONST && third->op == OP_ADD) return true;
    return false;
}

static bool is_jump(Instr* instr) {
    return instr->op == OP_JMP || instr->op == OP_JMP_IF_FALSE || instr->op == OP_JMP_IF_TRUE;
}

// Combines the pairs of the instructions: "get a local and a constant" is a
// very common sequence and the dispatch of the interpreter is not free.
static void peephole(FunctionProto* proto) {
    int count = proto->code_len;
    if (count < 2) return;

    Instr* source = (Instr*)xmalloc(sizeof(Instr) * (size_t)count);
    memcpy(source, proto->code, sizeof(Instr) * (size_t)count);

    // the map of the old offsets to the new ones
    int* offset_map = (int*)xmalloc(sizeof(int) * (size_t)(count + 1));

    int new_count = 0;
    for (int i = 0; i < count; i++) {
        offset_map[i] = new_count;
        if (i + 2 < count && can_combine_three(&source[i], &source[i + 1], &source[i + 2])) {
            Instr combined = source[i];
            combined.op = OP_GET_LOCAL_ADD;
            combined.a = (uint8_t)source[i].c;
            combined.c = source[i + 1].c;
            proto->code[new_count++] = combined;
            i += 2;
            continue;
        }
        if (i + 1 < count && can_combine(&source[i], &source[i + 1])) {
            Instr combined = source[i];
            if (source[i].op == OP_CONST) {
                // the constant and the addition: the constant is the first one
                combined.op = OP_ADD_CONST;
                combined.a = 0;
                combined.c = source[i].c;
            } else {
                combined.op = (source[i].op == OP_GET_LOCAL) ? OP_GET_LOCAL_CONST : OP_GET_GLOBAL_CONST;
                combined.a = (uint8_t)source[i].c;
                combined.c = source[i + 1].c;
            }
            proto->code[new_count++] = combined;
            i++;
            continue;
        }
        proto->code[new_count++] = source[i];
    }
    offset_map[count] = new_count;

    // the jumps have to be moved to the new offsets
    for (int i = 0; i < new_count; i++) {
        Instr* instr = &proto->code[i];
        if (is_jump(instr) && instr->c >= 0 && instr->c <= count) {
            instr->c = offset_map[instr->c];
        }
    }

    proto->code_len = new_count;
    free(source);
    free(offset_map);
}

// ------------------------------------------------------------------
// the registry of the classes
// ------------------------------------------------------------------

static ClassInfo* class_find(Compiler* compiler, const char* name, int length) {
    for (int i = 0; i < compiler->class_count; i++) {
        ClassInfo* info = compiler->classes[i];
        if ((int)strlen(info->name) == length && memcmp(info->name, name, (size_t)length) == 0) {
            return info;
        }
    }
    return NULL;
}

static ClassInfo* class_add(Compiler* compiler, const char* name, int length) {
    if (compiler->class_count == compiler->class_cap) {
        int capacity = compiler->class_cap ? compiler->class_cap * 2 : 8;
        compiler->classes = (ClassInfo**)xrealloc(compiler->classes,
                                                  sizeof(ClassInfo*) * (size_t)capacity);
        if (!compiler->classes) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        compiler->class_cap = capacity;
    }
    ClassInfo* info = (ClassInfo*)xcalloc_helper(sizeof(ClassInfo));
    info->name = (char*)xmalloc((size_t)length + 1);
    memcpy(info->name, name, (size_t)length);
    info->name[length] = '\0';
    compiler->classes[compiler->class_count++] = info;
    return info;
}

// ------------------------------------------------------------------
// the registry of the functions
// ------------------------------------------------------------------
//
// Every "def" of the top level of a module is registered here. The registry
// is used to inline the small functions at the place of their calls.

static FuncInfo* func_registry_add(Compiler* compiler, Node* def) {
    ModuleInfo* module = compiler->current;
    if (module->function_count == module->function_cap) {
        int capacity = module->function_cap ? module->function_cap * 2 : 8;
        module->function_list = (FuncInfo**)xrealloc(module->function_list,
                                                      sizeof(FuncInfo*) * (size_t)capacity);
        if (!module->function_list) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        module->function_cap = capacity;
    }
    FuncInfo* info = (FuncInfo*)xcalloc_helper(sizeof(FuncInfo));
    info->node = def;
    module->function_list[module->function_count] = info;
    int index = module->function_count++;
    // the last definition of a name is the one that is used
    map_put(&module->functions, def->name, def->name_len, index);
    return info;
}

static FuncInfo* func_registry_find(Compiler* compiler, const char* name, int length) {
    int index = map_get(&compiler->current->functions, name, length);
    if (index < 0) return NULL;
    return compiler->current->function_list[index];
}

// Frees the registry of the functions of a module.
static void module_free_functions(ModuleInfo* module) {
    for (int i = 0; i < module->function_count; i++) {
        free(module->function_list[i]->globals);
        free(module->function_list[i]);
    }
    free(module->function_list);
    module->function_list = NULL;
    module->function_count = 0;
    module->function_cap = 0;
    map_free(&module->functions);
}

// ------------------------------------------------------------------
// the analysis of the inlining
// ------------------------------------------------------------------
//
// A parameter of a function is stored in a local when the function is
// inlined and a struct is copied when it is stored in a local. The identity
// of the parameter cannot be seen when the parameter is only read: it is
// used as an operand, as a condition or as the base of a field. It can be
// seen when the value is stored somewhere, returned or given to another
// function: those functions are not inlined.

// The way a value is used: it is read or it is stored somewhere.
typedef enum { VALUE_READ, VALUE_TRANSFER } ValueUse;

// True when the node is one of the parameters of the function.
static bool node_is_param(Node* node, Node* def) {
    if (!node || node->type != ND_IDENT) return false;
    for (int i = 0; i < def->child_count; i++) {
        Node* param = def->children[i];
        if (param->name_len == node->name_len &&
            memcmp(param->name, node->name, (size_t)node->name_len) == 0) {
            return true;
        }
    }
    return false;
}

// True when the target of an assignment uses one of the parameters.
static bool target_uses_param(Node* target, Node* def) {
    if (!target) return false;
    switch (target->type) {
        case ND_IDENT: return node_is_param(target, def);
        case ND_FIELD:
        case ND_INDEX: return target_uses_param(target->left, def);
        default: return false;
    }
}

// True when the callee of a call is a function of the virtual machine. The
// native functions never modify their arguments.
static int resolve_local_in(FunctionState* fn, const char* name, int length);

static bool is_native_callee(Compiler* compiler, Node* callee) {
    if (!callee || callee->type != ND_IDENT) return false;
    if (resolve_local_in(compiler->fn, callee->name, callee->name_len) >= 0) return false;
    int index = map_get(&compiler->current->globals, callee->name, callee->name_len);
    if (index < 0) return false;
    return index < compiler->program->builtin_count;
}

// True when the body of the function can raise a runtime error that depends
// on the data: a division by zero, an index that is out of range or an error
// inside another function. The stack trace of such an error shows the frame
// of the function, so the function is not inlined.
static bool body_can_fail(Compiler* compiler, Node* node) {
    if (!node) return false;
    switch (node->type) {
        case ND_NUMBER:
        case ND_FLOAT:
        case ND_STRING:
        case ND_FSTRING:
        case ND_BOOL:
        case ND_NIL:
        case ND_IDENT:
            return false;
        case ND_BINARY:
            // a division by zero
            if (node->op == TK_SLASH || node->op == TK_PERCENT) return true;
            return body_can_fail(compiler, node->left) ||
                   body_can_fail(compiler, node->right);
        case ND_INDEX:
            // an index that is out of range or a key that is missing
            return true;
        case ND_ASSIGN:
        case ND_AUG_ASSIGN:
            if (node->left && node->left->type == ND_INDEX) return true;
            return body_can_fail(compiler, node->right);
        case ND_CALL:
            // the callee can fail: the functions of the user are not inlined
            if (!is_native_callee(compiler, node->left)) return true;
            return body_can_fail(compiler, node->left);
        case ND_METHOD:
            // the method can fail
            return true;
        case ND_UNARY:
            return body_can_fail(compiler, node->right);
        case ND_LOGICAL:
            return body_can_fail(compiler, node->left) ||
                   body_can_fail(compiler, node->right);
        case ND_TERNARY:
            return body_can_fail(compiler, node->left) ||
                   body_can_fail(compiler, node->then_branch) ||
                   body_can_fail(compiler, node->else_branch);
        case ND_FIELD:
            return body_can_fail(compiler, node->left);
        case ND_LIST:
        case ND_DICT:
            for (int i = 0; i < node->child_count; i++) {
                if (body_can_fail(compiler, node->children[i])) return true;
            }
            return false;
        case ND_VAR_DECL:
        case ND_TYPED_DECL:
            return body_can_fail(compiler, node->right);
        case ND_IF:
            return body_can_fail(compiler, node->left) ||
                   body_can_fail(compiler, node->then_branch) ||
                   body_can_fail(compiler, node->else_branch);
        case ND_WHILE:
            return body_can_fail(compiler, node->left) ||
                   body_can_fail(compiler, node->then_branch);
        case ND_FOR:
            return body_can_fail(compiler, node->right) ||
                   body_can_fail(compiler, node->then_branch);
        case ND_SWITCH:
            if (body_can_fail(compiler, node->left)) return true;
            for (int i = 0; i < node->child_count; i++) {
                if (body_can_fail(compiler, node->children[i])) return true;
            }
            return false;
        case ND_TRY:
            return body_can_fail(compiler, node->then_branch) ||
                   body_can_fail(compiler, node->else_branch);
        case ND_BLOCK:
            for (int i = 0; i < node->child_count; i++) {
                if (body_can_fail(compiler, node->children[i])) return true;
            }
            return false;
        case ND_EXPR_STMT:
            return body_can_fail(compiler, node->left);
        case ND_RETURN:
            return body_can_fail(compiler, node->left);
        case ND_BREAK:
        case ND_CONTINUE:
            return false;
        default:
            // an unknown node: the function is not inlined
            return true;
    }
}

// True when the body of the function uses a parameter in a way that can see
// the identity of the value.
static bool params_are_aliased(Compiler* compiler, Node* node, Node* def, ValueUse use) {
    if (!node) return false;
    switch (node->type) {
        case ND_IDENT:
            return use == VALUE_TRANSFER && node_is_param(node, def);
        case ND_NUMBER:
        case ND_FLOAT:
        case ND_STRING:
        case ND_FSTRING:
        case ND_BOOL:
        case ND_NIL:
            return false;
        case ND_BINARY:
            // the result is a new value: the operands are only read
            return params_are_aliased(compiler, node->left, def, VALUE_READ) ||
                   params_are_aliased(compiler, node->right, def, VALUE_READ);
        case ND_UNARY:
            return params_are_aliased(compiler, node->right, def, VALUE_READ);
        case ND_LOGICAL:
            // the result can be one of the operands
            return params_are_aliased(compiler, node->left, def, VALUE_TRANSFER) ||
                   params_are_aliased(compiler, node->right, def, VALUE_TRANSFER);
        case ND_TERNARY:
            return params_are_aliased(compiler, node->left, def, VALUE_READ) ||
                   params_are_aliased(compiler, node->then_branch, def, VALUE_TRANSFER) ||
                   params_are_aliased(compiler, node->else_branch, def, VALUE_TRANSFER);
        case ND_FIELD:
            // the value of a field is read: the copy of a struct keeps the fields
            return params_are_aliased(compiler, node->left, def, VALUE_READ);
        case ND_INDEX:
            return params_are_aliased(compiler, node->left, def, VALUE_READ) ||
                   params_are_aliased(compiler, node->right, def, VALUE_READ);
        case ND_CALL: {
            if (params_are_aliased(compiler, node->left, def, VALUE_READ)) return true;
            // the arguments are given to another function: a function of the
            // user can modify them
            bool native = is_native_callee(compiler, node->left);
            for (int i = 0; i < node->child_count; i++) {
                ValueUse arg_use = native ? VALUE_READ : VALUE_TRANSFER;
                if (params_are_aliased(compiler, node->children[i], def, arg_use)) return true;
            }
            return false;
        }
        case ND_METHOD:
            // the receiver can be modified by the method
            if (params_are_aliased(compiler, node->left, def, VALUE_TRANSFER)) return true;
            for (int i = 0; i < node->child_count; i++) {
                if (params_are_aliased(compiler, node->children[i], def, VALUE_TRANSFER)) return true;
            }
            return false;
        case ND_ASSIGN:
        case ND_AUG_ASSIGN:
            if (target_uses_param(node->left, def)) return true;
            return params_are_aliased(compiler, node->right, def, VALUE_TRANSFER);
        case ND_LIST:
        case ND_DICT:
            for (int i = 0; i < node->child_count; i++) {
                if (params_are_aliased(compiler, node->children[i], def, VALUE_TRANSFER)) return true;
            }
            return false;
        case ND_VAR_DECL:
        case ND_TYPED_DECL:
            return params_are_aliased(compiler, node->right, def, VALUE_TRANSFER);
        case ND_RETURN:
            return params_are_aliased(compiler, node->left, def, VALUE_TRANSFER);
        case ND_IF:
            return params_are_aliased(compiler, node->left, def, VALUE_READ) ||
                   params_are_aliased(compiler, node->then_branch, def, VALUE_TRANSFER) ||
                   params_are_aliased(compiler, node->else_branch, def, VALUE_TRANSFER);
        case ND_WHILE:
            return params_are_aliased(compiler, node->left, def, VALUE_READ) ||
                   params_are_aliased(compiler, node->then_branch, def, VALUE_TRANSFER);
        case ND_FOR:
            return params_are_aliased(compiler, node->right, def, VALUE_READ) ||
                   params_are_aliased(compiler, node->then_branch, def, VALUE_TRANSFER);
        case ND_SWITCH: {
            if (params_are_aliased(compiler, node->left, def, VALUE_READ)) return true;
            for (int i = 0; i < node->child_count; i++) {
                if (params_are_aliased(compiler, node->children[i], def, VALUE_TRANSFER)) return true;
            }
            return false;
        }
        case ND_TRY:
            return params_are_aliased(compiler, node->then_branch, def, VALUE_TRANSFER) ||
                   params_are_aliased(compiler, node->else_branch, def, VALUE_TRANSFER);
        case ND_BLOCK:
            for (int i = 0; i < node->child_count; i++) {
                if (params_are_aliased(compiler, node->children[i], def, VALUE_TRANSFER)) return true;
            }
            return false;
        case ND_EXPR_STMT:
            return params_are_aliased(compiler, node->left, def, VALUE_TRANSFER);
        case ND_BREAK:
        case ND_CONTINUE:
            return false;
        default:
            // an unknown node: the function is not inlined
            return true;
    }
}

// True when the body of the function has a "def" inside it: the closures
// capture the variables of the function and they cannot be inlined.
static bool node_has_def(Node* node) {
    if (!node) return false;
    if (node->type == ND_DEF) return true;
    if (node_has_def(node->left)) return true;
    if (node_has_def(node->right)) return true;
    if (node_has_def(node->then_branch)) return true;
    if (node_has_def(node->else_branch)) return true;
    for (int i = 0; i < node->child_count; i++) {
        if (node_has_def(node->children[i])) return true;
    }
    return false;
}

// ------------------------------------------------------------------
// diagnostics
// ------------------------------------------------------------------

static void compile_error(Compiler* compiler, int line, const char* fmt, ...) {
    char message[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    if (compiler->on_error) {
        compiler->on_error(compiler->error_user, line, message);
    } else {
        term_color(stderr, TERM_DIM);
        fprintf(stderr, "%s:%d: ", compiler->file_name, line);
        term_reset(stderr);
        term_color(stderr, TERM_BRED);
        fprintf(stderr, "error");
        term_reset(stderr);
        fprintf(stderr, ": %s\n", message);
    }
    longjmp(compiler->error_jump, 1);
}

// ------------------------------------------------------------------
// code emission
// ------------------------------------------------------------------

static Instr* code_at(FunctionProto* proto, int index) {
    return &proto->code[index];
}

static void emit_at(Compiler* compiler, OpCode op, int line) {
    FunctionProto* proto = compiler->fn->proto;
    if (proto->code_len == proto->code_cap) {
        int capacity = proto->code_cap ? proto->code_cap * 2 : 64;
        proto->code = (Instr*)xrealloc(proto->code, sizeof(Instr) * (size_t)capacity);
        proto->code_cap = capacity;
    }
    Instr* instr = &proto->code[proto->code_len++];
    instr->op = (uint8_t)op;
    instr->a = 0;
    instr->b = 0;
    instr->_pad = 0;
    instr->c = 0;
    instr->line = line;
}

static void emit_c(Compiler* compiler, OpCode op, int32_t operand, int line) {
    emit_at(compiler, op, line);
    code_at(compiler->fn->proto, compiler->fn->proto->code_len - 1)->c = operand;
}

static void emit_a(Compiler* compiler, OpCode op, uint8_t operand, int line) {
    emit_at(compiler, op, line);
    code_at(compiler->fn->proto, compiler->fn->proto->code_len - 1)->a = operand;
}

static void emit_ac(Compiler* compiler, OpCode op, uint8_t a, int32_t c, int line) {
    emit_at(compiler, op, line);
    Instr* instr = code_at(compiler->fn->proto, compiler->fn->proto->code_len - 1);
    instr->a = a;
    instr->c = c;
}

static int emit_jump(Compiler* compiler, OpCode op, int line) {
    emit_at(compiler, op, line);
    return compiler->fn->proto->code_len - 1;
}

static void patch_jump(Compiler* compiler, int position, int target) {
    code_at(compiler->fn->proto, position)->c = target;
}

static int add_constant(Compiler* compiler, Value value) {
    FunctionProto* proto = compiler->fn->proto;
    for (int i = 0; i < proto->const_count; i++) {
        if (values_equal(proto->constants[i], value)) return i;
    }
    if (proto->const_count == proto->const_cap) {
        int capacity = proto->const_cap ? proto->const_cap * 2 : 16;
        proto->constants = (Value*)xrealloc(proto->constants, sizeof(Value) * (size_t)capacity);
        proto->const_cap = capacity;
    }
    proto->constants[proto->const_count] = value;
    return proto->const_count++;
}

static void emit_constant(Compiler* compiler, Value value, int line) {
    int index = add_constant(compiler, value);
    emit_c(compiler, OP_CONST, index, line);
}

// ------------------------------------------------------------------
// scopes and variables
// ------------------------------------------------------------------

static bool is_module_scope(Compiler* compiler) {
    return compiler->fn->module_level;
}

static void push_scope(Compiler* compiler) {
    FunctionState* fn = compiler->fn;
    fn->scope_depth++;
    if (fn->scope_depth >= MAX_SCOPES) {
        compile_error(compiler, 0, "too many nested blocks");
    }
    fn->scope_bases[fn->scope_depth] = fn->local_count;
    fn->scope_captured[fn->scope_depth] = false;
}

static void pop_scope(Compiler* compiler, int line) {
    FunctionState* fn = compiler->fn;
    if (fn->scope_captured[fn->scope_depth]) {
        emit_c(compiler, OP_CLOSE_UPVAL, fn->scope_bases[fn->scope_depth], line);
    }
    while (fn->local_count > 0 && fn->locals[fn->local_count - 1].depth >= fn->scope_depth) {
        fn->local_count--;
    }
    fn->scope_depth--;
}

static int add_local(Compiler* compiler, const char* name, int length) {
    FunctionState* fn = compiler->fn;
    if (fn->local_count >= MAX_LOCALS) {
        compile_error(compiler, 0, "too many local variables in a function");
    }
    Local* local = &fn->locals[fn->local_count];
    local->name = arena_strndup(compiler->arena, name, (size_t)length);
    local->name_len = length;
    local->depth = fn->scope_depth;
    local->captured = false;

    int slot = fn->local_count++;
    if (fn->proto->stack_size < fn->local_count) {
        fn->proto->stack_size = fn->local_count;
    }
    return slot;
}

static int resolve_local_in(FunctionState* fn, const char* name, int length) {
    for (int i = fn->local_count - 1; i >= 0; i--) {
        if (fn->locals[i].name_len == length &&
            memcmp(fn->locals[i].name, name, (size_t)length) == 0) {
            return i;
        }
    }
    return -1;
}

static int add_upvalue_to(Compiler* compiler, FunctionState* fn, int index, bool is_local,
                          const char* name, int length) {
    for (int i = 0; i < fn->upvalue_count; i++) {
        if (fn->upvalues[i].index == index && fn->upvalues[i].is_local == is_local) return i;
    }
    if (fn->upvalue_count >= MAX_UPVALUES) {
        compile_error(compiler, 0, "too many captured variables");
    }
    Upvalue* upvalue = &fn->upvalues[fn->upvalue_count];
    upvalue->index = index;
    upvalue->is_local = is_local;
    upvalue->name = arena_strndup(compiler->arena, name, (size_t)length);
    upvalue->name_len = length;
    fn->upvalue_count++;
    fn->proto->upvalue_count = fn->upvalue_count;
    return fn->upvalue_count - 1;
}

static int resolve_upvalue_in(Compiler* compiler, FunctionState* fn, const char* name, int length) {
    for (int i = 0; i < fn->upvalue_count; i++) {
        if (fn->upvalues[i].name_len == length &&
            memcmp(fn->upvalues[i].name, name, (size_t)length) == 0) {
            return i;
        }
    }
    if (!fn->enclosing) return -1;

    int local = resolve_local_in(fn->enclosing, name, length);
    if (local >= 0) {
        fn->enclosing->locals[local].captured = true;
        fn->enclosing->scope_captured[fn->enclosing->locals[local].depth] = true;
        return add_upvalue_to(compiler, fn, local, true, name, length);
    }
    int upvalue = resolve_upvalue_in(compiler, fn->enclosing, name, length);
    if (upvalue >= 0) {
        return add_upvalue_to(compiler, fn, upvalue, false, name, length);
    }
    return -1;
}

// Adds a name to the list of the globals that the body of a function uses.
static void collect_global(Compiler* compiler, const char* name, int length) {
    FunctionState* fn = compiler->fn;
    if (!fn->collect_globals) return;
    for (int i = 0; i < fn->free_global_count; i++) {
        if (fn->free_globals[i].name_len == length &&
            memcmp(fn->free_globals[i].name, name, (size_t)length) == 0) {
            return;
        }
    }
    if (fn->free_global_count >= fn->free_global_cap) {
        int capacity = fn->free_global_cap ? fn->free_global_cap * 2 : 8;
        fn->free_globals = (NameRef*)xrealloc(fn->free_globals,
                                              sizeof(NameRef) * (size_t)capacity);
        if (!fn->free_globals) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        fn->free_global_cap = capacity;
    }
    fn->free_globals[fn->free_global_count].name = name;
    fn->free_globals[fn->free_global_count].name_len = length;
    fn->free_global_count++;
}

static bool resolve_variable(Compiler* compiler, const char* name, int length, VarRef* ref) {
    // the globals of the function that is inlined are always the globals of
    // the module: a local of the caller cannot hide them
    for (int i = 0; i < compiler->forced_global_count; i++) {
        const NameRef* global = &compiler->forced_globals[i];
        if (global->name_len != length ||
            memcmp(global->name, name, (size_t)length) != 0) {
            continue;
        }
        int index = map_get(&compiler->current->globals, name, length);
        if (index >= 0) {
            ref->kind = VAR_GLOBAL;
            ref->index = index;
            return true;
        }
        break;
    }

    int local = resolve_local_in(compiler->fn, name, length);
    if (local >= 0) {
        ref->kind = VAR_LOCAL;
        ref->index = local;
        return true;
    }
    int upvalue = resolve_upvalue_in(compiler, compiler->fn, name, length);
    if (upvalue >= 0) {
        ref->kind = VAR_UPVAL;
        ref->index = upvalue;
        return true;
    }
    int global = map_get(&compiler->current->globals, name, length);
    if (global >= 0) {
        ref->kind = VAR_GLOBAL;
        ref->index = global;
        collect_global(compiler, name, length);
        return true;
    }
    return false;
}

static int declare_global(Compiler* compiler, const char* name, int length) {
    int existing = map_get(&compiler->current->globals, name, length);
    if (existing >= compiler->program->builtin_count) return existing;

    int index = compiler->program->global_count;
    program_add_global_name(compiler->program, name, length);
    map_put(&compiler->current->globals, name, length, index);
    return index;
}

// ------------------------------------------------------------------
// loops
// ------------------------------------------------------------------

static void push_loop(Compiler* compiler, int continue_target, int body_depth, bool has_iterator) {
    FunctionState* fn = compiler->fn;
    if (fn->loop_count >= MAX_LOOPS) {
        compile_error(compiler, 0, "too many nested loops");
    }
    LoopCtx* ctx = &fn->loops[fn->loop_count++];
    ctx->continue_target = continue_target;
    ctx->body_depth = body_depth;
    ctx->has_iterator = has_iterator;
    ctx->break_count = 0;
}

static void pop_loop(Compiler* compiler, int break_target) {
    FunctionState* fn = compiler->fn;
    LoopCtx* ctx = &fn->loops[--fn->loop_count];
    for (int i = 0; i < ctx->break_count; i++) {
        patch_jump(compiler, ctx->breaks[i], break_target);
    }
}

// closes every scope that is left when a loop is left through break/continue
static void close_scopes_to(Compiler* compiler, int body_depth, int line) {
    FunctionState* fn = compiler->fn;
    for (int depth = fn->scope_depth; depth >= body_depth; depth--) {
        if (fn->scope_captured[depth]) {
            emit_c(compiler, OP_CLOSE_UPVAL, fn->scope_bases[depth], line);
        }
    }
}

// ------------------------------------------------------------------
// statements
// ------------------------------------------------------------------

static void compile_statement(Compiler* compiler, Node* node);
static void compile_try(Compiler* compiler, Node* node);
static void compile_expression(Compiler* compiler, Node* node);
static void compile_block(Compiler* compiler, Node* block);
static void compile_import(Compiler* compiler, Node* node);
static bool resolve_module_member(Compiler* compiler, Node* target, int* global_index);
static void inline_block_push(Compiler* compiler, Node* def);
static void inline_block_pop(Compiler* compiler, Node* def);
static bool try_inline_call(Compiler* compiler, Node* call);

// binds a name and discards the assigned value (it stays on the stack)
static void bind_name(Compiler* compiler, const char* name, int length, int line) {
    if (is_module_scope(compiler)) {
        int index = declare_global(compiler, name, length);
        emit_c(compiler, OP_SET_GLOBAL, index, line);
    } else {
        int slot = add_local(compiler, name, length);
        emit_c(compiler, OP_SET_LOCAL, slot, line);
    }
    emit_at(compiler, OP_POP, line);
}

static void compile_var_decl(Compiler* compiler, Node* node) {
    if (node->right) compile_expression(compiler, node->right);
    else emit_at(compiler, OP_NIL, node->line);
    bind_name(compiler, node->name, node->name_len, node->line);
}

// the static type of a literal expression, -1 when it is not known
static int literal_value_type(Node* node) {
    switch (node->type) {
        case ND_NUMBER: return VAL_INT;
        case ND_FLOAT:  return VAL_FLOAT;
        case ND_STRING: return VAL_STRING;
        case ND_BOOL:   return VAL_BOOL;
        default:        return -1;
    }
}

// can a value of the type "from" be assigned to a variable of the type "to"
static bool type_assignable(int from, int to) {
    if (from == to) return true;
    if (from == VAL_INT && (to == VAL_FLOAT || to == VAL_FLOAT32)) return true;
    if (from == VAL_FLOAT && to == VAL_FLOAT32) return true;
    return false;
}

static void compile_typed_decl(Compiler* compiler, Node* node) {
    ValueType type = (ValueType)node->value_type;

    if (node->right) {
        compile_expression(compiler, node->right);
        int literal = literal_value_type(node->right);
        if (literal >= 0) {
            if (!type_assignable(literal, (int)type)) {
                compile_error(compiler, node->line, "cannot initialize '%s' with '%s'",
                              value_type_name(type), value_type_name((ValueType)literal));
            }
            if (literal != (int)type) {
                emit_c(compiler, OP_CAST, (int32_t)type, node->line);
            }
        } else {
            emit_c(compiler, OP_CAST, (int32_t)type, node->line);
        }
    } else {
        switch (type) {
            case VAL_INT:
                emit_constant(compiler, value_int(0), node->line);
                break;
            case VAL_FLOAT:
                emit_constant(compiler, value_float(0.0), node->line);
                break;
            case VAL_FLOAT32:
                emit_constant(compiler, value_float32(0.0), node->line);
                break;
            case VAL_STRING: {
                ObjString* empty = string_new(compiler->vm, "", 0);
                emit_constant(compiler, value_obj(&empty->obj), node->line);
                break;
            }
            case VAL_BOOL:
                emit_at(compiler, OP_FALSE, node->line);
                break;
            case VAL_LIST:
                emit_a(compiler, OP_LIST, 0, node->line);
                break;
            case VAL_DICT:
                emit_a(compiler, OP_DICT, 0, node->line);
                break;
            default:
                break;
        }
    }

    bind_name(compiler, node->name, node->name_len, node->line);
}

// A constant that holds a field name, used by OP_GET_FIELD / OP_SET_FIELD.
static int field_constant(Compiler* compiler, const char* name, int length) {
    ObjString* str = string_new(compiler->vm, name, (size_t)length);
    return add_constant(compiler, value_obj(&str->obj));
}

// Starts a function whose first local slot holds "self".
static void push_method_scope(Compiler* compiler, FunctionProto* proto) {
    FunctionState* enclosing = compiler->fn;
    if (compiler->fn_depth + 1 >= MAX_FN_DEPTH) {
        compile_error(compiler, 0, "too many nested function definitions");
    }
    FunctionState* fn = &compiler->fn_stack[++compiler->fn_depth];
    memset(fn, 0, sizeof(*fn));
    fn->proto = proto;
    fn->enclosing = enclosing;
    compiler->fn = fn;
    push_scope(compiler);
    add_local(compiler, "self", 4); // slot 0
}

static void pop_method_scope(Compiler* compiler) {
    pop_scope(compiler, 0);
    compiler->fn = compiler->fn->enclosing;
    compiler->fn_depth--;
}

// Compiles the default values of the fields into the current method.
static void compile_field_defaults(Compiler* compiler, Node* class_node) {
    for (int i = 0; i < class_node->child_count; i++) {
        Node* field = class_node->children[i];
        if (!field->right) continue;

        emit_c(compiler, OP_GET_LOCAL, 0, field->line); // self
        compile_expression(compiler, field->right);
        emit_c(compiler, OP_SET_FIELD, field_constant(compiler, field->name, field->name_len),
               field->line);
    }
}

// Compiles a method of a class. When class_node is given the default values
// of the fields are compiled at the beginning of the method.
static int compile_method(Compiler* compiler, Node* node, Node* class_node) {
    FunctionProto* proto = (FunctionProto*)arena_calloc(compiler->arena, 1, sizeof(FunctionProto));
    proto->name = arena_strndup(compiler->arena, node->name, (size_t)node->name_len);
    proto->arity = node->child_count;
    int proto_index = program_add_proto(compiler->program, proto);

    push_method_scope(compiler, proto);
    for (int i = 0; i < node->child_count; i++) {
        Node* param = node->children[i];
        add_local(compiler, param->name, param->name_len);
    }

    if (class_node) compile_field_defaults(compiler, class_node);

    Node* body = node->then_branch;
    for (int i = 0; i < body->child_count; i++) {
        compile_statement(compiler, body->children[i]);
    }

    emit_at(compiler, OP_NIL, node->line);
    emit_at(compiler, OP_RETURN, node->line);
    pop_method_scope(compiler);

    proto->upvalues = (UpvalueDesc*)xmalloc(sizeof(UpvalueDesc) * (size_t)(proto->upvalue_count + 1));
    return proto_index;
}

static void compile_class(Compiler* compiler, Node* node) {
    ClassInfo* info = class_add(compiler, node->name, node->name_len);
    info->is_struct = node->is_mutable;

    // the parent class: its fields and methods are inherited
    ClassInfo* parent = NULL;
    if (node->str_val) {
        parent = class_find(compiler, node->str_val, node->str_len);
        if (!parent) {
            compile_error(compiler, node->line, "unknown parent class '%s'", node->str_val);
        }
        info->parent = parent->name;
    }

    // --- the fields: the ones of the parent, then the ones of the class ---
    int field_count = 0;
    int parent_fields = parent ? parent->field_count : 0;
    int own_fields = node->child_count;
    field_count = parent_fields + own_fields;

    char** field_names = (char**)xcalloc_helper(sizeof(char*) * (size_t)(field_count + 1));
    for (int i = 0; i < parent_fields; i++) {
        field_names[i] = parent->field_names[i];
    }
    for (int i = 0; i < own_fields; i++) {
        Node* field = node->children[i];
        field_names[parent_fields + i] = arena_strndup(compiler->arena, field->name,
                                                      (size_t)field->name_len);
    }

    // --- the default values of the fields ---
    int default_count = (parent ? parent->default_count : 0) + own_fields;
    Node** defaults = (Node**)xcalloc_helper(sizeof(Node*) * (size_t)(default_count + 1));
    int default_index = 0;
    if (parent) {
        for (int i = 0; i < parent->default_count; i++) {
            defaults[default_index++] = parent->defaults[i];
        }
    }
    for (int i = 0; i < own_fields; i++) {
        defaults[default_index++] = node->children[i];
    }

    // --- the methods: the ones of the parent, then the ones of the class ---
    Node* methods = node->then_branch;
    int own_methods = methods->child_count;
    int parent_methods = parent ? parent->method_count : 0;

    int method_count = parent_methods + own_methods;
    char** method_names = (char**)xcalloc_helper(sizeof(char*) * (size_t)(method_count + 1));
    int* method_protos = (int*)xcalloc_helper(sizeof(int) * (size_t)(method_count + 1));
    int method_index = 0;

    // the methods of the parent are inherited as they are
    for (int i = 0; i < parent_methods; i++) {
        method_names[method_index] = parent->method_names[i];
        method_protos[method_index] = parent->method_protos[i];
        method_index++;
    }

    // the constructor: the one of the class, or the one of the parent
    Node* init_node = NULL;
    for (int i = 0; i < own_methods; i++) {
        Node* method = methods->children[i];
        if (method->name_len == 4 && memcmp(method->name, "init", 4) == 0) {
            init_node = method;
        }
    }
    if (!init_node && parent) init_node = parent->init_node;

    // the methods of the class: the ones that override the parent are replaced
    for (int i = 0; i < own_methods; i++) {
        Node* method = methods->children[i];
        if (init_node == method) continue; // the constructor is compiled apart

        int replaced = -1;
        for (int j = 0; j < method_index; j++) {
            if ((int)strlen(method_names[j]) == method->name_len &&
                memcmp(method_names[j], method->name, (size_t)method->name_len) == 0) {
                replaced = j;
                break;
            }
        }
        if (replaced >= 0) {
            method_names[replaced] = arena_strndup(compiler->arena, method->name,
                                                   (size_t)method->name_len);
            method_protos[replaced] = compile_method(compiler, method, NULL);
            continue;
        }
        method_names[method_index] = arena_strndup(compiler->arena, method->name,
                                                   (size_t)method->name_len);
        method_protos[method_index] = compile_method(compiler, method, NULL);
        method_index++;
    }

    // --- the constructor with the default values of the fields ---
    int init_proto = -1;
    bool has_defaults = false;
    for (int i = 0; i < default_count; i++) {
        if (defaults[i] && defaults[i]->right) has_defaults = true;
    }
    if (init_node || has_defaults) {
        FunctionProto* proto = (FunctionProto*)arena_calloc(compiler->arena, 1, sizeof(FunctionProto));
        proto->name = arena_strdup(compiler->arena, "<init>");
        proto->arity = init_node ? init_node->child_count : 0;
        int proto_index = program_add_proto(compiler->program, proto);

        push_method_scope(compiler, proto);
        for (int i = 0; i < default_count; i++) {
            Node* field = defaults[i];
            if (!field || !field->right) continue;
            emit_c(compiler, OP_GET_LOCAL, 0, field->line); // self
            compile_expression(compiler, field->right);
            emit_c(compiler, OP_SET_FIELD, field_constant(compiler, field->name, field->name_len),
                   field->line);
        }
        if (init_node) {
            for (int i = 0; i < init_node->child_count; i++) {
                Node* param = init_node->children[i];
                add_local(compiler, param->name, param->name_len);
            }
            Node* body = init_node->then_branch;
            for (int i = 0; i < body->child_count; i++) {
                compile_statement(compiler, body->children[i]);
            }
        }
        emit_at(compiler, OP_NIL, node->line);
        emit_at(compiler, OP_RETURN, node->line);
        pop_method_scope(compiler);
        proto->upvalues = (UpvalueDesc*)xmalloc(sizeof(UpvalueDesc));
        peephole(proto);
        init_proto = proto_index;
    }

    // --- the class itself ---
    ClassProto* klass = (ClassProto*)arena_calloc(compiler->arena, 1, sizeof(ClassProto));
    klass->name = arena_strndup(compiler->arena, node->name, (size_t)node->name_len);
    klass->is_struct = node->is_mutable;
    klass->field_count = (uint32_t)field_count;
    klass->field_names = (char**)arena_calloc(compiler->arena, sizeof(char*), (size_t)(field_count + 1));
    for (int i = 0; i < field_count; i++) klass->field_names[i] = field_names[i];
    klass->method_count = (uint32_t)method_index;
    klass->method_names = (char**)arena_calloc(compiler->arena, sizeof(char*), (size_t)(method_index + 1));
    klass->method_protos = (int*)arena_calloc(compiler->arena, sizeof(int), (size_t)(method_index + 1));
    for (int i = 0; i < method_index; i++) {
        klass->method_names[i] = method_names[i];
        klass->method_protos[i] = method_protos[i];
    }
    klass->init_proto = init_proto;
    klass->parent = parent ? arena_strdup(compiler->arena, parent->name) : NULL;

    int class_index = program_add_class(compiler->program, klass);
    emit_c(compiler, OP_CLASS, class_index, node->line);
    bind_name(compiler, node->name, node->name_len, node->line);

    // the information for the classes that inherit from this one
    info->field_count = field_count;
    info->field_names = field_names;
    info->method_count = method_index;
    info->method_names = method_names;
    info->method_protos = method_protos;
    info->defaults = defaults;
    info->default_count = default_count;
    info->init_node = init_node;
}

// switch value { case 1: { } default: { } }
// the cases fall through to the next one, like in C without "break"
static void compile_switch(Compiler* compiler, Node* node) {
    compile_expression(compiler, node->left);
    int value_slot = add_local(compiler, "__sps_switch", 12);
    emit_c(compiler, OP_SET_LOCAL, value_slot, node->line);

    for (int i = 0; i < node->child_count; i++) {
        Node* branch = node->children[i];
        if (branch->left) {
            emit_c(compiler, OP_GET_LOCAL, value_slot, node->line);
            compile_expression(compiler, branch->left);
            emit_at(compiler, OP_EQ, node->line);
            int next_branch = emit_jump(compiler, OP_JMP_IF_FALSE, node->line);
            compile_block(compiler, branch->then_branch);
            patch_jump(compiler, next_branch, compiler->fn->proto->code_len);
        } else {
            compile_block(compiler, branch->then_branch);
        }
    }
}

static void compile_try(Compiler* compiler, Node* node) {
    int try_patch = emit_jump(compiler, OP_TRY, node->line);
    compiler->fn->try_depth++;

    compile_block(compiler, node->then_branch);
    emit_at(compiler, OP_TRY_END, node->line);
    compiler->fn->try_depth--;
    int end_jump = emit_jump(compiler, OP_JMP, node->line);

    // the handler: the message of the error is on the stack
    int handler_offset = compiler->fn->proto->code_len;
    patch_jump(compiler, try_patch, handler_offset);

    push_scope(compiler);
    int slot = add_local(compiler, node->name, node->name_len);
    emit_c(compiler, OP_SET_LOCAL, slot, node->line);
    emit_at(compiler, OP_POP, node->line);
    compile_block(compiler, node->else_branch);
    pop_scope(compiler, node->line);

    patch_jump(compiler, end_jump, compiler->fn->proto->code_len);
}

static void compile_def(Compiler* compiler, Node* node) {
    // the name is reserved before the body is compiled so that the function
    // is able to call itself (recursion)
    bool module_scope = is_module_scope(compiler);
    int target = 0;
    if (node->name) {
        target = module_scope ? declare_global(compiler, node->name, node->name_len)
                              : add_local(compiler, node->name, node->name_len);
    }

    FunctionProto* proto = (FunctionProto*)arena_calloc(compiler->arena, 1, sizeof(FunctionProto));
    proto->name = arena_strndup(compiler->arena, node->name, (size_t)node->name_len);
    proto->arity = node->child_count;
    int proto_index = program_add_proto(compiler->program, proto);

    FunctionState* enclosing = compiler->fn;
    if (compiler->fn_depth + 1 >= MAX_FN_DEPTH) {
        compile_error(compiler, node->line, "too many nested function definitions");
    }
    FunctionState* fn = &compiler->fn_stack[++compiler->fn_depth];
    memset(fn, 0, sizeof(*fn));
    fn->proto = proto;
    fn->enclosing = enclosing;
    compiler->fn = fn;

    // the function of the top level of a module is registered before its body
    // is compiled: the other functions are able to use it
    FuncInfo* info = NULL;
    if (module_scope && node->name) {
        info = func_registry_add(compiler, node);
        info->has_closure = node_has_def(node->then_branch);
    }
    // the function cannot be inlined while its body is compiled: the
    // recursion would never end
    inline_block_push(compiler, node);
    fn->collect_globals = (info != NULL);

    push_scope(compiler);
    for (int i = 0; i < node->child_count; i++) {
        Node* param = node->children[i];
        add_local(compiler, param->name, param->name_len);
    }

    Node* body = node->then_branch;
    for (int i = 0; i < body->child_count; i++) {
        compile_statement(compiler, body->children[i]);
    }

    fn->collect_globals = false;
    inline_block_pop(compiler, node);

    emit_at(compiler, OP_NIL, node->line);
    emit_at(compiler, OP_RETURN, node->line);
    pop_scope(compiler, node->line);

    if (info) {
        info->has_upvalue = fn->upvalue_count > 0;
        info->params_safe = !params_are_aliased(compiler, body, node, VALUE_TRANSFER);
        info->can_fail = body_can_fail(compiler, body);
        info->globals = fn->free_globals;
        info->global_count = fn->free_global_count;
        fn->free_globals = NULL;
        fn->free_global_count = 0;
        fn->free_global_cap = 0;
    }

    proto->upvalues = (UpvalueDesc*)xmalloc(sizeof(UpvalueDesc) * (size_t)(fn->upvalue_count + 1));
    for (int i = 0; i < fn->upvalue_count; i++) {
        proto->upvalues[i].index = fn->upvalues[i].index;
        proto->upvalues[i].is_local = fn->upvalues[i].is_local;
        proto->upvalues[i].name = fn->upvalues[i].name;
        proto->upvalues[i].name_len = fn->upvalues[i].name_len;
    }

    compiler->fn = enclosing;
    compiler->fn_depth--;

    emit_c(compiler, OP_CLOSURE, proto_index, node->line);
    if (!node->name) {
        // an anonymous function: the closure is the value of the expression
        return;
    }
    if (module_scope) emit_c(compiler, OP_SET_GLOBAL, target, node->line);
    else emit_c(compiler, OP_SET_LOCAL, target, node->line);
    emit_at(compiler, OP_POP, node->line);
}

static void compile_if(Compiler* compiler, Node* node) {
    compile_expression(compiler, node->left);
    int else_jump = emit_jump(compiler, OP_JMP_IF_FALSE, node->line);
    compile_block(compiler, node->then_branch);
    int end_jump = emit_jump(compiler, OP_JMP, node->line);
    patch_jump(compiler, else_jump, compiler->fn->proto->code_len);

    if (node->else_branch) {
        if (node->else_branch->type == ND_BLOCK) {
            compile_block(compiler, node->else_branch);
        } else {
            compile_statement(compiler, node->else_branch);
        }
    }
    patch_jump(compiler, end_jump, compiler->fn->proto->code_len);
}

static void compile_while(Compiler* compiler, Node* node) {
    int loop_start = compiler->fn->proto->code_len;
    compile_expression(compiler, node->left);
    int exit_jump = emit_jump(compiler, OP_JMP_IF_FALSE, node->line);

    push_loop(compiler, loop_start, compiler->fn->scope_depth + 1, false);
    compile_block(compiler, node->then_branch);
    emit_c(compiler, OP_JMP, loop_start, node->line);
    patch_jump(compiler, exit_jump, compiler->fn->proto->code_len);
    pop_loop(compiler, compiler->fn->proto->code_len);
}

static void compile_for(Compiler* compiler, Node* node) {
    push_scope(compiler);

    compile_expression(compiler, node->right); // the iterable
    emit_at(compiler, OP_ITER_INIT, node->line);

    int loop_start = compiler->fn->proto->code_len;
    int next_jump = emit_jump(compiler, OP_ITER_NEXT, node->line);

    push_loop(compiler, loop_start, compiler->fn->scope_depth + 1, true);

    int var_slot = add_local(compiler, node->left->name, node->left->name_len);
    emit_c(compiler, OP_SET_LOCAL, var_slot, node->line);
    emit_at(compiler, OP_POP, node->line);

    compile_block(compiler, node->then_branch);

    emit_c(compiler, OP_JMP, loop_start, node->line);
    patch_jump(compiler, next_jump, compiler->fn->proto->code_len);
    pop_loop(compiler, compiler->fn->proto->code_len);
    emit_at(compiler, OP_POP, node->line); // drop the iterator
    pop_scope(compiler, node->line);
}

static void compile_break(Compiler* compiler, Node* node) {
    FunctionState* fn = compiler->fn;
    if (fn->loop_count == 0) {
        compile_error(compiler, node->line, "'break' outside of a loop");
    }
    LoopCtx* ctx = &fn->loops[fn->loop_count - 1];
    close_scopes_to(compiler, ctx->body_depth, node->line);
    int jump = emit_jump(compiler, OP_JMP, node->line);
    if (ctx->break_count >= MAX_BREAKS) {
        compile_error(compiler, node->line, "too many break statements");
    }
    ctx->breaks[ctx->break_count++] = jump;
}

static void compile_continue(Compiler* compiler, Node* node) {
    FunctionState* fn = compiler->fn;
    if (fn->loop_count == 0) {
        compile_error(compiler, node->line, "'continue' outside of a loop");
    }
    LoopCtx* ctx = &fn->loops[fn->loop_count - 1];
    close_scopes_to(compiler, ctx->body_depth, node->line);
    emit_c(compiler, OP_JMP, ctx->continue_target, node->line);
}

static void compile_return(Compiler* compiler, Node* node) {
    if (node->left) compile_expression(compiler, node->left);
    else emit_at(compiler, OP_NIL, node->line);

    FunctionState* fn = compiler->fn;
    if (fn->inline_count == 0) {
        emit_at(compiler, OP_RETURN, node->line);
        return;
    }

    // the return is inside an inlined block: the value stays on the stack and
    // the jump goes to the end of the block
    InlineCtx* ctx = &fn->inlines[fn->inline_count - 1];

    // the error handlers of the block are removed
    for (int i = ctx->try_base; i < fn->try_depth; i++) {
        emit_at(compiler, OP_TRY_END, node->line);
    }

    // the iterators of the "for" loops are removed: the value of the return
    // is kept in a local while they are popped
    int iterators = 0;
    for (int i = ctx->loop_base; i < fn->loop_count; i++) {
        if (fn->loops[i].has_iterator) iterators++;
    }
    if (iterators > 0) {
        // the value of the return is on the top of the stack, above the
        // iterators of the loops
        emit_c(compiler, OP_SET_LOCAL, ctx->temp_slot, node->line);
        for (int i = 0; i <= iterators; i++) {
            emit_at(compiler, OP_POP, node->line);
        }
        emit_c(compiler, OP_GET_LOCAL, ctx->temp_slot, node->line);
    }

    // the jump to the end of the block
    int jump = emit_jump(compiler, OP_JMP, node->line);
    if (ctx->return_count >= ctx->return_cap) {
        ctx->return_cap = ctx->return_cap ? ctx->return_cap * 2 : 8;
        ctx->returns = (int*)xrealloc(ctx->returns, sizeof(int) * (size_t)ctx->return_cap);
        if (!ctx->returns) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
    }
    ctx->returns[ctx->return_count++] = jump;
}

static void compile_block(Compiler* compiler, Node* block) {
    push_scope(compiler);
    for (int i = 0; i < block->child_count; i++) {
        compile_statement(compiler, block->children[i]);
    }
    pop_scope(compiler, block->line);
}

static void compile_statement(Compiler* compiler, Node* node) {
    switch (node->type) {
        case ND_VAR_DECL:  compile_var_decl(compiler, node); break;
        case ND_TYPED_DECL: compile_typed_decl(compiler, node); break;
        case ND_CLASS:      compile_class(compiler, node); break;
        case ND_TRY:        compile_try(compiler, node); break;
        case ND_SWITCH:     compile_switch(compiler, node); break;
        case ND_IMPORT:    compile_import(compiler, node); break;
        case ND_DEF:       compile_def(compiler, node); break;
        case ND_IF:        compile_if(compiler, node); break;
        case ND_WHILE:     compile_while(compiler, node); break;
        case ND_FOR:       compile_for(compiler, node); break;
        case ND_RETURN:    compile_return(compiler, node); break;
        case ND_BREAK:     compile_break(compiler, node); break;
        case ND_CONTINUE:  compile_continue(compiler, node); break;
        case ND_BLOCK:     compile_block(compiler, node); break;
        case ND_EXPR_STMT:
            compile_expression(compiler, node->left);
            emit_at(compiler, OP_POP, node->line);
            break;
        default:
            compile_error(compiler, node->line, "unsupported statement");
            break;
    }
}

// ------------------------------------------------------------------
// expressions
// ------------------------------------------------------------------

static void compile_identifier(Compiler* compiler, Node* node) {
    VarRef ref;
    if (!resolve_variable(compiler, node->name, node->name_len, &ref)) {
        compile_error(compiler, node->line, "undefined variable '%s'", node->name);
    }
    switch (ref.kind) {
        case VAR_LOCAL:  emit_c(compiler, OP_GET_LOCAL, ref.index, node->line); break;
        case VAR_UPVAL:  emit_c(compiler, OP_GET_UPVAL, ref.index, node->line); break;
        case VAR_GLOBAL: emit_c(compiler, OP_GET_GLOBAL, ref.index, node->line); break;
    }
}

static void compile_store(Compiler* compiler, Node* target, int line) {
    if (target->type == ND_IDENT) {
        VarRef ref;
        if (!resolve_variable(compiler, target->name, target->name_len, &ref)) {
            compile_error(compiler, line, "undefined variable '%s'", target->name);
        }
        switch (ref.kind) {
            case VAR_LOCAL:  emit_c(compiler, OP_SET_LOCAL, ref.index, line); break;
            case VAR_UPVAL:  emit_c(compiler, OP_SET_UPVAL, ref.index, line); break;
            case VAR_GLOBAL: emit_c(compiler, OP_SET_GLOBAL, ref.index, line); break;
        }
        return;
    }
    compile_error(compiler, line, "invalid assignment target");
}

static void compile_assignment(Compiler* compiler, Node* node) {
    Node* target = node->left;
    if (target->type == ND_FIELD) {
        int global_index = 0;
        if (resolve_module_member(compiler, target, &global_index)) {
            compile_expression(compiler, node->right);
            emit_c(compiler, OP_SET_GLOBAL, global_index, node->line);
            return;
        }
        compile_expression(compiler, target->left);
        compile_expression(compiler, node->right);
        emit_c(compiler, OP_SET_FIELD,
               field_constant(compiler, target->name, target->name_len), node->line);
        return;
    }
    if (target->type == ND_INDEX) {
        compile_expression(compiler, target->left);
        compile_expression(compiler, target->right);
        compile_expression(compiler, node->right);
        emit_at(compiler, OP_SET_INDEX, node->line);
        return;
    }
    compile_expression(compiler, node->right);
    compile_store(compiler, target, node->line);
}

static void compile_augmented_assignment(Compiler* compiler, Node* node) {
    Node* target = node->left;
    OpCode op;
    switch (node->op) {
        case TK_PLUS:    op = OP_ADD; break;
        case TK_MINUS:   op = OP_SUB; break;
        case TK_STAR:    op = OP_MUL; break;
        case TK_SLASH:   op = OP_DIV; break;
        default:         op = OP_MOD; break;
    }

    if (target->type == ND_FIELD) {
        compile_expression(compiler, target->left);
        emit_at(compiler, OP_DUP, node->line);
        emit_c(compiler, OP_GET_FIELD,
               field_constant(compiler, target->name, target->name_len), node->line);
        compile_expression(compiler, node->right);
        emit_at(compiler, op, node->line);
        emit_c(compiler, OP_SET_FIELD,
               field_constant(compiler, target->name, target->name_len), node->line);
        return;
    }

    if (target->type == ND_INDEX) {
        compile_expression(compiler, target->left);
        compile_expression(compiler, target->right);
        emit_at(compiler, OP_GET_INDEX_KEEP, node->line);
        compile_expression(compiler, node->right);
        emit_at(compiler, op, node->line);
        emit_at(compiler, OP_SET_INDEX, node->line);
        return;
    }

    compile_expression(compiler, target);
    compile_expression(compiler, node->right);
    emit_at(compiler, op, node->line);
    compile_store(compiler, target, node->line);
}

static void compile_logical(Compiler* compiler, Node* node) {
    bool is_and = (node->op == TK_AND || node->op == TK_AND_AND);
    compile_expression(compiler, node->left);
    // the jump pops the duplicated value, so the left operand stays on the
    // stack when the short circuit is taken
    emit_at(compiler, OP_DUP, node->line);
    int jump = emit_jump(compiler, is_and ? OP_JMP_IF_FALSE : OP_JMP_IF_TRUE, node->line);
    emit_at(compiler, OP_POP, node->line);
    compile_expression(compiler, node->right);
    patch_jump(compiler, jump, compiler->fn->proto->code_len);
}

static void compile_expression(Compiler* compiler, Node* node) {
    switch (node->type) {
        case ND_NUMBER:
            emit_constant(compiler, value_int(node->int_val), node->line);
            break;
        case ND_FLOAT:
            emit_constant(compiler, value_float(node->float_val), node->line);
            break;
        case ND_STRING: {
            ObjString* str = string_new(compiler->vm, node->str_val, (size_t)node->str_len);
            emit_constant(compiler, value_obj(&str->obj), node->line);
            break;
        }
case ND_FSTRING: {
            for (int i = 0; i < node->child_count; i++) {
                Node* part = node->children[i];
                if (part->type == ND_STRING) {
                    ObjString* str = string_new(compiler->vm, part->str_val, (size_t)part->str_len);
                    emit_constant(compiler, value_obj(&str->obj), node->line);
                    continue;
                }
                // an interpolated expression
                compile_expression(compiler, part->left);
                int spec_index = -1;
                if (part->str_len > 0) {
                    ObjString* spec = string_new(compiler->vm, part->str_val, (size_t)part->str_len);
                    spec_index = add_constant(compiler, value_obj(&spec->obj));
                }
                emit_c(compiler, OP_FORMAT, spec_index, node->line);
            }
            for (int i = 1; i < node->child_count; i++) {
                emit_at(compiler, OP_ADD, node->line);
            }
            break;
        }
        case ND_BOOL:
            emit_at(compiler, node->bool_val ? OP_TRUE : OP_FALSE, node->line);
            break;
        case ND_NIL:
            emit_at(compiler, OP_NIL, node->line);
            break;
        case ND_IDENT:
            compile_identifier(compiler, node);
            break;
        case ND_LIST: {
            for (int i = 0; i < node->child_count; i++) {
                compile_expression(compiler, node->children[i]);
            }
            emit_a(compiler, OP_LIST, (uint8_t)node->child_count, node->line);
            break;
        }
        case ND_DICT: {
            for (int i = 0; i + 1 < node->child_count; i += 2) {
                compile_expression(compiler, node->children[i]);
                compile_expression(compiler, node->children[i + 1]);
            }
            emit_a(compiler, OP_DICT, (uint8_t)(node->child_count / 2), node->line);
            break;
        }
        case ND_BINARY: {
            compile_expression(compiler, node->left);
            compile_expression(compiler, node->right);
            OpCode op;
            switch (node->op) {
                case TK_PLUS:    op = OP_ADD;    break;
                case TK_MINUS:   op = OP_SUB;    break;
                case TK_STAR:    op = OP_MUL;    break;
                case TK_SLASH:   op = OP_DIV;    break;
                case TK_PERCENT: op = OP_MOD;    break;
                case TK_EQ_EQ:   op = OP_EQ;     break;
                case TK_NE:      op = OP_NE;     break;
                case TK_LT:      op = OP_LT;     break;
                case TK_LE:      op = OP_LE;     break;
                case TK_GT:      op = OP_GT;     break;
                case TK_GE:      op = OP_GE;     break;
                case TK_IN:      op = OP_IN;     break;
                case TK_AMP:     op = OP_BITAND; break;
                case TK_PIPE:    op = OP_BITOR;  break;
                case TK_CARET:   op = OP_BITXOR; break;
                case TK_SHL:     op = OP_SHL;    break;
                case TK_SHR:     op = OP_SHR;    break;
                default:
                    compile_error(compiler, node->line, "unsupported binary operator");
                    return;
            }
            emit_at(compiler, op, node->line);
            break;
        }
        case ND_UNARY: {
            compile_expression(compiler, node->right);
            if (node->op == TK_MINUS) {
                emit_at(compiler, OP_NEG, node->line);
            } else if (node->op == TK_PLUS) {
                // unary plus is a no-op
            } else {
                emit_at(compiler, OP_NOT, node->line);
            }
            break;
        }
        case ND_LOGICAL:
            compile_logical(compiler, node);
            break;
        case ND_TERNARY: {
            compile_expression(compiler, node->left);
            int else_jump = emit_jump(compiler, OP_JMP_IF_FALSE, node->line);
            compile_expression(compiler, node->right);
            int end_jump = emit_jump(compiler, OP_JMP, node->line);
            patch_jump(compiler, else_jump, compiler->fn->proto->code_len);
            compile_expression(compiler, node->then_branch);
            patch_jump(compiler, end_jump, compiler->fn->proto->code_len);
            break;
        }
        case ND_CALL: {
            if (try_inline_call(compiler, node)) break;
            compile_expression(compiler, node->left);
            for (int i = 0; i < node->child_count; i++) {
                compile_expression(compiler, node->children[i]);
            }
            emit_a(compiler, OP_CALL, (uint8_t)node->child_count, node->line);
            break;
        }
        case ND_DEF: {
            // an anonymous function: the closure is the value of the expression
            compile_def(compiler, node);
            break;
        }
        case ND_METHOD: {
            int module_global = 0;
            if (resolve_module_member(compiler, node, &module_global)) {
                // a function of an imported module
                emit_c(compiler, OP_GET_GLOBAL, module_global, node->line);
                for (int i = 0; i < node->child_count; i++) {
                    compile_expression(compiler, node->children[i]);
                }
                emit_a(compiler, OP_CALL, (uint8_t)node->child_count, node->line);
                break;
            }

            int group = method_group_find(node->name, node->name_len);
            if (group >= 0) {
                compile_expression(compiler, node->left);
                for (int i = 0; i < node->child_count; i++) {
                    compile_expression(compiler, node->children[i]);
                }
                emit_ac(compiler, OP_METHOD, (uint8_t)node->child_count, group, node->line);
            } else {
                // a method of a user defined class, the name is resolved at runtime
                ObjString* str = string_new(compiler->vm, node->name, (size_t)node->name_len);
                int name_constant = add_constant(compiler, value_obj(&str->obj));
                compile_expression(compiler, node->left);
                for (int i = 0; i < node->child_count; i++) {
                    compile_expression(compiler, node->children[i]);
                }
                emit_ac(compiler, OP_METHOD_NAME, (uint8_t)node->child_count, name_constant,
                        node->line);
            }
            break;
        }
        case ND_FIELD: {
            int global_index = 0;
            if (resolve_module_member(compiler, node, &global_index)) {
                emit_c(compiler, OP_GET_GLOBAL, global_index, node->line);
                break;
            }
            compile_expression(compiler, node->left);
            emit_c(compiler, OP_GET_FIELD,
                   field_constant(compiler, node->name, node->name_len), node->line);
            break;
        }
        case ND_INDEX:
            compile_expression(compiler, node->left);
            compile_expression(compiler, node->right);
            emit_at(compiler, OP_GET_INDEX, node->line);
            break;
        case ND_ASSIGN:
            compile_assignment(compiler, node);
            break;
        case ND_AUG_ASSIGN:
            compile_augmented_assignment(compiler, node);
            break;
        default:
            compile_error(compiler, node->line, "unsupported expression");
            break;
    }
}

// ------------------------------------------------------------------
// the inlining of the calls
// ------------------------------------------------------------------
//
// The small functions of a module are compiled inside the caller: the call
// is replaced by the body of the function. The block leaves one value on the
// stack, exactly like a call does.

// The functions that are compiled right now cannot be inlined: the recursion
// would never end.
static void inline_block_push(Compiler* compiler, Node* def) {
    if (compiler->inline_block_count >= MAX_INLINE_BLOCKS) {
        compile_error(compiler, 0, "too many nested function definitions");
    }
    compiler->inline_blocks[compiler->inline_block_count++] = def;
}

static void inline_block_pop(Compiler* compiler, Node* def) {
    (void)def;
    if (compiler->inline_block_count > 0) compiler->inline_block_count--;
}

// True when the function is compiled right now: it is already inlined or
// its own body is being compiled.
static bool inline_blocked(Compiler* compiler, Node* def) {
    for (int i = 0; i < compiler->inline_block_count; i++) {
        if (compiler->inline_blocks[i] == def) return true;
    }
    return false;
}

// Compiles the body of the function inside the caller.
static void compile_inline_call(Compiler* compiler, Node* call, FuncInfo* info) {
    Node* def = info->node;
    int argc = call->child_count;

    // the arguments are evaluated in the scope of the caller, in order
    for (int i = 0; i < argc; i++) {
        compile_expression(compiler, call->children[i]);
    }

    FunctionState* fn = compiler->fn;
    bool module_level = fn->module_level;
    const NameRef* forced = compiler->forced_globals;
    int forced_count = compiler->forced_global_count;

    // the body of a function is not the top level of a module: the local
    // variables of the function do not leak into the caller
    fn->module_level = false;
    // the globals of the function are the globals of the module: a local of
    // the caller cannot hide them
    compiler->forced_globals = info->globals;
    compiler->forced_global_count = info->global_count;

    push_scope(compiler);

    // the parameters are the locals of the new scope
    int slots[MAX_INLINE_PARAMS];
    for (int i = 0; i < argc; i++) {
        Node* param = def->children[i];
        slots[i] = add_local(compiler, param->name, param->name_len);
    }
    // the values are moved from the stack into the locals: the last argument
    // is on the top of the stack
    for (int i = argc - 1; i >= 0; i--) {
        emit_c(compiler, OP_SET_LOCAL, slots[i], call->line);
        emit_at(compiler, OP_POP, call->line);
    }

    InlineCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.loop_base = fn->loop_count;
    ctx.try_base = fn->try_depth;
    // the local that keeps the value of a return while the iterators of the
    // loops are removed
    ctx.temp_slot = add_local(compiler, "__sps_ret", 9);
    fn->inlines[fn->inline_count++] = ctx;

    // the function cannot be inlined again while its body is compiled
    inline_block_push(compiler, def);

    Node* body = def->then_branch;
    for (int i = 0; i < body->child_count; i++) {
        compile_statement(compiler, body->children[i]);
    }

    inline_block_pop(compiler, def);

    // the body can finish without a return: the value of the block is nil
    emit_at(compiler, OP_NIL, call->line);
    int end = compiler->fn->proto->code_len;

    // the returns of the body jump to the end of the block
    InlineCtx* current = &fn->inlines[--fn->inline_count];
    for (int i = 0; i < current->return_count; i++) {
        patch_jump(compiler, current->returns[i], end);
    }
    free(current->returns);

    pop_scope(compiler, call->line);

    fn->module_level = module_level;
    compiler->forced_globals = forced;
    compiler->forced_global_count = forced_count;
}

// Tries to compile the body of the function inside the caller. Returns true
// when the call was inlined.
static bool try_inline_call(Compiler* compiler, Node* call) {
    Node* callee = call->left;
    if (!callee || callee->type != ND_IDENT) return false;

    FuncInfo* info = func_registry_find(compiler, callee->name, callee->name_len);
    if (!info) return false;

    Node* def = info->node;
    if (info->has_closure) return false;  // a closure inside the body
    if (info->has_upvalue) return false;  // a captured variable
    if (!info->params_safe) return false; // the identity of a parameter
    if (info->can_fail) return false;     // the stack trace of an error
    if (def->child_count != call->child_count) return false; // the arguments
    if (def->child_count > MAX_INLINE_PARAMS) return false;
    if (def->then_branch->child_count > MAX_INLINE_BODY) return false;
    if (inline_blocked(compiler, def)) return false; // the recursion
    if (compiler->fn->inline_count >= MAX_INLINE_DEPTH) return false;

    compile_inline_call(compiler, call, info);
    return true;
}

// ------------------------------------------------------------------
// entry point
// ------------------------------------------------------------------

// ------------------------------------------------------------------
// modules
// ------------------------------------------------------------------

// The folders where the packages are searched: the "lib" of the project.
static char** g_search_paths = NULL;
static int g_search_path_count = 0;

void compiler_set_search_paths(const char* const* paths, int count) {
    g_search_paths = (char**)paths;
    g_search_path_count = count;
}

static bool file_exists(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

// Builds the path of an imported file: the folder of the importing file
// first, then the packages of the project.
static char* resolve_import_path(Compiler* compiler, const char* path) {
    if (path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':')) {
        return arena_strdup(compiler->arena, path);
    }

    const char* current = compiler->current->path;
    const char* slash = strrchr(current, '/');
    const char* backslash = strrchr(current, '\\');
    const char* dir_end;
    if (slash && (!backslash || slash > backslash)) dir_end = slash + 1;
    else if (backslash) dir_end = backslash + 1;
    else return arena_strdup(compiler->arena, path);

    size_t dir_len = (size_t)(dir_end - current);
    size_t len = strlen(path);
    char* result = (char*)arena_alloc(compiler->arena, dir_len + len + 1);
    memcpy(result, current, dir_len);
    memcpy(result + dir_len, path, len + 1);

    if (file_exists(result)) return result;

    // the packages of the project
    for (int i = 0; i < g_search_path_count; i++) {
        size_t base_len = strlen(g_search_paths[i]);
        char* candidate = (char*)arena_alloc(compiler->arena, base_len + len + 2);
        memcpy(candidate, g_search_paths[i], base_len);
        candidate[base_len] = '/';
        memcpy(candidate + base_len + 1, path, len + 1);
        if (file_exists(candidate)) return candidate;
    }
    return result;
}

static char* read_source_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) size = 0;
    char* buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[read] = '\0';
    return buffer;
}

// Compiles the top level code of a module and returns the index of its
// entry prototype.
static int compile_module_body(Compiler* compiler, Node* ast) {
    FunctionProto* entry = (FunctionProto*)arena_calloc(compiler->arena, 1, sizeof(FunctionProto));
    entry->name = arena_strdup(compiler->arena, "<module>");
    int entry_index = program_add_proto(compiler->program, entry);

    FunctionState* saved_fn = compiler->fn;
    FunctionState* fn = &compiler->fn_stack[compiler->fn_depth++];
    memset(fn, 0, sizeof(*fn));
    fn->proto = entry;
    fn->enclosing = NULL;
    fn->module_level = true;
    compiler->fn = fn;

    push_scope(compiler);
    for (int i = 0; i < ast->child_count; i++) {
        compile_statement(compiler, ast->children[i]);
    }
    emit_at(compiler, OP_NIL, ast->line);
    emit_at(compiler, OP_RETURN, ast->line);
    pop_scope(compiler, ast->line);

    compiler->fn_depth--;
    compiler->fn = saved_fn;
    return entry_index;
}

// Finds a module that was imported with the given alias.
static ModuleInfo* find_alias(Compiler* compiler, const char* name, int length) {
    ModuleInfo* module = compiler->current;
    for (int i = 0; i < module->alias_count; i++) {
        AliasInfo* alias = &module->aliases[i];
        if ((int)strlen(alias->alias) == length && memcmp(alias->alias, name, (size_t)length) == 0) {
            return &compiler->modules[alias->module];
        }
    }
    return NULL;
}

// Resolves "module.symbol" - returns false when the receiver is not a module.
static bool resolve_module_member(Compiler* compiler, Node* target, int* global_index) {
    if (!target->left || target->left->type != ND_IDENT) return false;
    ModuleInfo* module = find_alias(compiler, target->left->name, target->left->name_len);
    if (!module) return false;

    int index = map_get(&module->globals, target->name, target->name_len);
    if (index < 0) {
        compile_error(compiler, target->line, "module '%s' has no symbol '%s'",
                     target->left->name, target->name);
    }
    *global_index = index;
    return true;
}

static void compile_import(Compiler* compiler, Node* node) {
    char* path = resolve_import_path(compiler, node->str_val);

    ModuleInfo* module = NULL;
    for (int i = 0; i < compiler->module_count; i++) {
        if (strcmp(compiler->modules[i].path, path) == 0) {
            module = &compiler->modules[i];
            break;
        }
    }

    if (!module) {
        char* source = read_source_file(path);
        if (!source) {
            compile_error(compiler, node->line, "cannot open '%s'", path);
        }

        Parser parser;
        Node* ast = parser_parse(&parser, compiler->arena, source);
        free(source);
        if (!ast) {
            compile_error(compiler, node->line, "cannot parse '%s'", path);
        }

        if (compiler->module_count >= compiler->module_cap) {
            int capacity = compiler->module_cap ? compiler->module_cap * 2 : 8;
            compiler->modules = (ModuleInfo*)xrealloc(compiler->modules,
                                                      sizeof(ModuleInfo) * (size_t)capacity);
            compiler->module_cap = capacity;
        }

        module = &compiler->modules[compiler->module_count++];
        memset(module, 0, sizeof(*module));
        module->path = path;
        module->alias = node->name ? arena_strndup(compiler->arena, node->name,
                                                   (size_t)node->name_len) : NULL;
        map_init(&module->globals);
        map_init(&module->functions);
        seed_module_globals(compiler, module);

        ModuleInfo* previous = compiler->current;
        compiler->current = module;
        module->entry_proto = compile_module_body(compiler, ast);
        compiler->current = previous;
    }

    // the alias is visible in the module that imports it
    if (node->name) {
        ModuleInfo* current = compiler->current;
        if (current->alias_count < (int)(sizeof(current->aliases) / sizeof(current->aliases[0]))) {
            AliasInfo* alias = &current->aliases[current->alias_count++];
            alias->alias = arena_strndup(compiler->arena, node->name, (size_t)node->name_len);
            alias->module = (int)(module - compiler->modules);
        }
    } else {
        // without an alias the symbols of the module are used directly
        map_copy(&module->globals, &compiler->current->globals);
    }

    emit_c(compiler, OP_IMPORT, module->entry_proto, node->line);
}

bool compiler_compile(Program* program, VM* vm, Node* ast, const char* file_name) {
    return compiler_compile_ex(program, vm, ast, file_name, NULL, NULL);
}

bool compiler_compile_diagnostic(Program* program, VM* vm, Node* ast, const char* file_name) {
    return compiler_compile_ex(program, vm, ast, file_name, NULL, NULL);
}

bool compiler_compile_ex(Program* program, VM* vm, Node* ast, const char* file_name,
                         SpsCompileErrorHandler on_error, void* user) {
    Compiler compiler;
    memset(&compiler, 0, sizeof(compiler));
    compiler.arena = &program->arena;
    compiler.program = program;
    compiler.vm = vm;
    compiler.module_cap = 8;
    compiler.modules = (ModuleInfo*)xcalloc_helper(sizeof(ModuleInfo) * 8);
    compiler.on_error = on_error;
    compiler.error_user = user;
    snprintf(compiler.file_name, sizeof(compiler.file_name), "%s", file_name ? file_name : "<script>");

    if (setjmp(compiler.error_jump) != 0) {
        for (int i = 0; i < compiler.module_count; i++) {
            map_free(&compiler.modules[i].globals);
            module_free_functions(&compiler.modules[i]);
        }
        free(compiler.modules);
        return false;
    }

    ModuleInfo* main_module = &compiler.modules[compiler.module_count++];
    memset(main_module, 0, sizeof(*main_module));
    main_module->path = arena_strdup(&program->arena, file_name ? file_name : "<main>");
    map_init(&main_module->globals);
    map_init(&main_module->functions);
    compiler.current = main_module;

    // the native functions of the vm become the first globals
    seed_module_globals(&compiler, main_module);
    // then the classes of the host
    for (int i = 0; i < vm->host_class_count; i++) {
        const char* name = vm->host_classes[i]->name;
        program_add_global_name(program, name, (int)strlen(name));
        map_put(&main_module->globals, name, (int)strlen(name), vm->native_count + i);
    }
    program->builtin_count = vm->native_count;
    int main_index = map_get(&main_module->globals, "main", 4);

    // the body of the main module
    FunctionProto* entry = (FunctionProto*)arena_calloc(&program->arena, 1, sizeof(FunctionProto));
    entry->name = arena_strdup(&program->arena, "<module>");
    program->entry_proto = program_add_proto(program, entry);

    FunctionState* fn = &compiler.fn_stack[0];
    memset(fn, 0, sizeof(*fn));
    fn->proto = entry;
    fn->enclosing = NULL;
    fn->module_level = true;
    compiler.fn = fn;
    compiler.fn_depth = 1;

    push_scope(&compiler);
    for (int i = 0; i < ast->child_count; i++) {
        compile_statement(&compiler, ast->children[i]);
    }

    if (main_index >= 0) {
        emit_c(&compiler, OP_GET_GLOBAL, main_index, ast->line);
        emit_a(&compiler, OP_CALL, 0, ast->line);
        emit_at(&compiler, OP_POP, ast->line);
    }

    emit_at(&compiler, OP_NIL, ast->line);
    emit_at(&compiler, OP_RETURN, ast->line);
    pop_scope(&compiler, ast->line);

    for (int i = 0; i < compiler.module_count; i++) {
        map_free(&compiler.modules[i].globals);
        module_free_functions(&compiler.modules[i]);
    }
    free(compiler.modules);
    return true;
}
