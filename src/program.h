// Spectra3D Script - compiled program (function prototypes, global variables)
#ifndef SPS_PROGRAM_H
#define SPS_PROGRAM_H

#include "arena.h"
#include "opcode.h"
#include "value.h"

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
