// Spectra3D Script - compiled program (function prototypes, global variables)
#ifndef SPS_PROGRAM_H
#define SPS_PROGRAM_H

#include "internal/arena.h"
#include "internal/value.h"

#include <stdint.h>

// ------------------------------------------------------------------
// Opcode definitions (inlined from opcode.h)
// ------------------------------------------------------------------

typedef enum {
    OP_FORMAT,         // c = spec constant (-1 = default) -> [value] -> [str]
    OP_IMPORT,         // c = module entry proto      -> run the module
    OP_TRY,            // c = handler offset -> register the error handler
    OP_TRY_END,         // remove the error handler
    OP_CLASS,          // c = class index              -> push class
    OP_GET_FIELD,       // c = name constant           [obj] -> [value]
    OP_SET_FIELD,       // c = name constant           [obj, value] -> [value]
    OP_CAST,           // c = target type; [value] -> [value]
    OP_CONST,          // c = constant index            -> push
    OP_NIL,            //                               -> push nil
    OP_TRUE,
    OP_FALSE,
    OP_POP,            //                               -> pop
    OP_DUP,            //                               -> dup
    OP_GET_LOCAL,      // c = slot index                -> push
    OP_GET_LOCAL_CONST, // a = slot, c = constant        -> push, push
    OP_GET_GLOBAL_CONST,// a = global, c = constant      -> push, push
    OP_GET_LOCAL_ADD,  // a = slot, c = constant         -> push (local + constant)
    OP_ADD_CONST,      // c = constant                    -> pop, push (value + constant)
    OP_SET_LOCAL,      // c = slot index                -> pop, store
    OP_GET_GLOBAL,     // c = global index              -> push
    OP_SET_GLOBAL,     // c = global index              -> pop, store
    OP_GET_UPVAL,      // c = upvalue index             -> push
    OP_SET_UPVAL,      // c = upvalue index             -> pop, store
    OP_NATIVE,         // c = builtin index             -> push native
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_NEG,
    OP_NOT,
    OP_IN,            // [value, container] -> [bool]
    OP_EQ,
    OP_NE,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,
    OP_BITAND,
    OP_BITOR,
    OP_BITXOR,
    OP_SHL,
    OP_SHR,
    OP_JMP,            // c = absolute target            -> jump
    OP_JMP_IF_FALSE,   // c = absolute target            -> jump if nil/false
    OP_JMP_IF_TRUE,
    OP_CALL,           // a = argument count             -> call
    OP_METHOD,         // a = argument count, c = method group -> method call
    OP_METHOD_NAME,   // a = argument count, c = name constant -> method call
    OP_ITER_INIT,     // [iterable] -> [iterator]
    OP_ITER_NEXT,     // c = end target; [iterator] -> [iterator, value]
    OP_RETURN,         //                               -> return
    OP_CLOSURE,        // c = prototype index            -> push closure
    OP_CLOSE_UPVAL,    // c = slot threshold             -> close upvalues
    OP_LIST,           // a = element count              -> build list
    OP_DICT,           // a = pair count                -> build dictionary
    OP_GET_INDEX,      // [obj, idx] -> [elem]
    OP_GET_INDEX_KEEP, // [obj, idx] -> [obj, idx, elem]
    OP_SET_INDEX,      // [obj, idx, val] -> [val]
    OP_LEN,            // [x] -> [len]
    OP_COUNT           // number of opcodes
} OpCode;

extern const char* const OP_NAMES[];

#include <stdio.h>

#define SPS_FILE_MAGIC   "SPSB"
#define SPS_FILE_VERSION 1u

typedef struct {
    uint8_t op;
    uint8_t a;
    uint8_t b;
    uint8_t _pad;
    int32_t c;
    int32_t line;
} Instr;

typedef struct {
    int   index;    // local slot or upvalue index in the enclosing function
    bool  is_local;
    char* name;
    int   name_len;
} UpvalueDesc;

typedef struct FunctionProto {
    char*         name;
    int           arity;
    int           upvalue_count;
    int           stack_size;   // maximum number of local slots
    Instr*        code;
    int           code_len;
    int           code_cap;
    Value*        constants;
    int           const_count;
    int           const_cap;
    UpvalueDesc*  upvalues;
} FunctionProto;

typedef struct {
    char*    name;
    bool     is_struct;
    uint32_t field_count;
    char**   field_names;
    uint32_t method_count;
    char**   method_names;
    int*     method_protos;
    int      init_proto; // the constructor with the default values, -1 when there is none
    char*    parent;     // the name of the parent class
} ClassProto;

typedef struct Program {
    Arena           arena;
    FunctionProto** protos;
    int             proto_count;
    int             proto_cap;
    ClassProto**    classes;
    int             class_count;
    int             class_cap;
    char**          global_names;
    int             global_count;
    int             global_cap;
    int             builtin_count;
    int             entry_proto;
} Program;

void program_init(Program* program);
void program_release(Program* program);

int  program_add_proto(Program* program, FunctionProto* proto);
int  program_add_class(Program* program, ClassProto* klass);
void program_add_global_name(Program* program, const char* name, int len);

bool program_serialize(Program* program, const char* path);
bool program_deserialize(Program* program, VM* vm, const char* path);

#endif /* SPS_PROGRAM_H */
