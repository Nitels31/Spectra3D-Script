// Spectra3D Script - compiled program container and .spc serialization
#include "internal/program.h"
#include "internal/builtins.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------
// OP_NAMES (moved from opcode.c)
// ------------------------------------------------------------------

const char* const OP_NAMES[] = {
    "FORMAT",
    "IMPORT",
    "TRY", "TRY_END",
    "CLASS", "GET_FIELD", "SET_FIELD", "CAST",
    "CONST", "NIL", "TRUE", "FALSE", "POP", "DUP",
    "GET_LOCAL", "GET_LOCAL_CONST", "GET_GLOBAL_CONST", "GET_LOCAL_ADD", "ADD_CONST", "SET_LOCAL", "GET_GLOBAL", "SET_GLOBAL",
    "GET_UPVAL", "SET_UPVAL", "NATIVE",
    "ADD", "SUB", "MUL", "DIV", "MOD", "NEG", "NOT",
    "IN", "EQ", "NE", "LT", "LE", "GT", "GE",
    "BITAND", "BITOR", "BITXOR", "SHL", "SHR",
    "JMP", "JMP_IF_FALSE", "JMP_IF_TRUE",
    "CALL", "METHOD", "METHOD_NAME", "ITER_INIT", "ITER_NEXT", "RETURN", "CLOSURE", "CLOSE_UPVAL", "LIST",
    "DICT", "GET_INDEX", "GET_INDEX_KEEP", "SET_INDEX", "LEN"
};

// the names must be in the same order as the opcode enum
_Static_assert(sizeof(OP_NAMES) / sizeof(OP_NAMES[0]) == OP_COUNT,
               "OP_NAMES must match the opcode enum");

void program_init(Program* program) {
    memset(program, 0, sizeof(*program));
    arena_init(&program->arena);
    program->builtin_count = SPS_BUILTIN_COUNT;
}

void program_release(Program* program) {
    for (int i = 0; i < program->proto_count; i++) {
        FunctionProto* proto = program->protos[i];
        free(proto->code);
        free(proto->constants);
        free(proto->upvalues);
    }
    free(program->protos);
    free(program->classes);
    free(program->global_names);
    arena_release(&program->arena);
    memset(program, 0, sizeof(*program));
}

int program_add_proto(Program* program, FunctionProto* proto) {
    if (program->proto_count == program->proto_cap) {
        int capacity = program->proto_cap ? program->proto_cap * 2 : 16;
        program->protos = (FunctionProto**)realloc(program->protos,
                                                   sizeof(FunctionProto*) * (size_t)capacity);
        if (!program->protos) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        program->proto_cap = capacity;
    }
    program->protos[program->proto_count] = proto;
    return program->proto_count++;
}

int program_add_class(Program* program, ClassProto* klass) {
    if (program->class_count == program->class_cap) {
        int capacity = program->class_cap ? program->class_cap * 2 : 8;
        program->classes = (ClassProto**)realloc(program->classes,
                                                 sizeof(ClassProto*) * (size_t)capacity);
        if (!program->classes) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        program->class_cap = capacity;
    }
    program->classes[program->class_count] = klass;
    return program->class_count++;
}

void program_add_global_name(Program* program, const char* name, int len) {    if (program->global_count == program->global_cap) {
        int capacity = program->global_cap ? program->global_cap * 2 : 32;
        program->global_names = (char**)realloc(program->global_names,
                                                 sizeof(char*) * (size_t)capacity);
        if (!program->global_names) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        program->global_cap = capacity;
    }
    program->global_names[program->global_count] = arena_strndup(&program->arena, name, (size_t)len);
    program->global_count++;
}

// ------------------------------------------------------------------
// binary helpers
// ------------------------------------------------------------------

static void write_u8(FILE* file, uint8_t value) {
    fwrite(&value, 1, 1, file);
}

static void write_u32(FILE* file, uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value & 0xff);
    bytes[1] = (uint8_t)((value >> 8) & 0xff);
    bytes[2] = (uint8_t)((value >> 16) & 0xff);
    bytes[3] = (uint8_t)((value >> 24) & 0xff);
    fwrite(bytes, 1, 4, file);
}

static void write_i32(FILE* file, int32_t value) {
    write_u32(file, (uint32_t)value);
}

static void write_i64(FILE* file, int64_t value) {
    uint8_t bytes[8];
    for (int i = 0; i < 8; i++) bytes[i] = (uint8_t)((uint64_t)value >> (i * 8));
    fwrite(bytes, 1, 8, file);
}

static void write_double(FILE* file, double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    write_i64(file, (int64_t)bits);
}

static bool read_u8(FILE* file, uint8_t* out) {
    return fread(out, 1, 1, file) == 1;
}

static bool read_u32(FILE* file, uint32_t* out) {
    uint8_t bytes[4];
    if (fread(bytes, 1, 4, file) != 4) return false;
    *out = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
    return true;
}

static bool read_i32(FILE* file, int32_t* out) {
    uint32_t value;
    if (!read_u32(file, &value)) return false;
    *out = (int32_t)value;
    return true;
}

static bool read_i64(FILE* file, int64_t* out) {
    uint8_t bytes[8];
    if (fread(bytes, 1, 8, file) != 8) return false;
    uint64_t bits = 0;
    for (int i = 7; i >= 0; i--) bits = (bits << 8) | bytes[i];
    *out = (int64_t)bits;
    return true;
}

static bool read_double(FILE* file, double* out) {
    int64_t bits;
    if (!read_i64(file, &bits)) return false;
    uint64_t raw = (uint64_t)bits;
    memcpy(out, &raw, sizeof(*out));
    return true;
}

// ------------------------------------------------------------------
// serialization
// ------------------------------------------------------------------

#define CONST_TAG_NIL   0
#define CONST_TAG_BOOL  1
#define CONST_TAG_INT   2
#define CONST_TAG_FLOAT 3
#define CONST_TAG_STRING 4

bool program_serialize(Program* program, const char* path) {
    FILE* file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "sps: cannot open '%s' for writing\n", path);
        return false;
    }

    fwrite(SPS_FILE_MAGIC, 1, 4, file);
    write_u32(file, SPS_FILE_VERSION);
    write_u32(file, (uint32_t)program->proto_count);

    for (int i = 0; i < program->proto_count; i++) {
        FunctionProto* proto = program->protos[i];
        size_t name_len = proto->name ? strlen(proto->name) : 0;
        write_u32(file, (uint32_t)name_len);
        if (name_len) fwrite(proto->name, 1, name_len, file);

        write_i32(file, proto->arity);
        write_i32(file, proto->upvalue_count);
        write_i32(file, proto->stack_size);

        write_u32(file, (uint32_t)proto->code_len);
        for (int j = 0; j < proto->code_len; j++) {
            Instr* instr = &proto->code[j];
            write_u8(file, instr->op);
            write_u8(file, instr->a);
            write_u8(file, instr->b);
            write_u8(file, 0);
            write_i32(file, instr->c);
            write_i32(file, instr->line);
        }

        write_u32(file, (uint32_t)proto->const_count);
        for (int j = 0; j < proto->const_count; j++) {
            Value constant = proto->constants[j];
            switch (constant.type) {
                case VAL_NIL:
                    write_u8(file, CONST_TAG_NIL);
                    break;
                case VAL_BOOL:
                    write_u8(file, CONST_TAG_BOOL);
                    write_u8(file, constant.as.boolean ? 1 : 0);
                    break;
                case VAL_INT:
                    write_u8(file, CONST_TAG_INT);
                    write_i64(file, constant.as.integer);
                    break;
                case VAL_FLOAT:
                    write_u8(file, CONST_TAG_FLOAT);
                    write_double(file, constant.as.number);
                    break;
                case VAL_STRING: {
                    ObjString* str = (ObjString*)constant.as.obj;
                    write_u8(file, CONST_TAG_STRING);
                    write_u32(file, str->len);
                    if (str->len) fwrite(str->chars, 1, str->len, file);
                    break;
                }
                default:
                    write_u8(file, CONST_TAG_NIL);
                    break;
            }
        }

        write_u32(file, (uint32_t)proto->upvalue_count);
        for (int j = 0; j < proto->upvalue_count; j++) {
            write_i32(file, proto->upvalues[j].index);
            write_u8(file, proto->upvalues[j].is_local ? 1 : 0);
        }
    }

    write_u32(file, (uint32_t)program->class_count);
    for (int i = 0; i < program->class_count; i++) {
        ClassProto* klass = program->classes[i];
        size_t name_len = strlen(klass->name);
        write_u32(file, (uint32_t)name_len);
        fwrite(klass->name, 1, name_len, file);
        write_u8(file, klass->is_struct ? 1 : 0);

        write_u32(file, klass->field_count);
        for (uint32_t j = 0; j < klass->field_count; j++) {
            size_t len = strlen(klass->field_names[j]);
            write_u32(file, (uint32_t)len);
            fwrite(klass->field_names[j], 1, len, file);
        }

        write_u32(file, klass->method_count);
        for (uint32_t j = 0; j < klass->method_count; j++) {
            size_t len = strlen(klass->method_names[j]);
            write_u32(file, (uint32_t)len);
            fwrite(klass->method_names[j], 1, len, file);
            write_i32(file, klass->method_protos[j]);
        }

        write_i32(file, klass->init_proto);
    }

    write_u32(file, (uint32_t)program->global_count);
    for (int i = 0; i < program->global_count; i++) {
        size_t name_len = strlen(program->global_names[i]);
        write_u32(file, (uint32_t)name_len);
        if (name_len) fwrite(program->global_names[i], 1, name_len, file);
    }

    write_u32(file, (uint32_t)program->entry_proto);
    fclose(file);
    return true;
}

bool program_deserialize(Program* program, VM* vm, const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "sps: cannot open '%s' for reading\n", path);
        return false;
    }

    char magic[4];
    if (fread(magic, 1, 4, file) != 4 || memcmp(magic, SPS_FILE_MAGIC, 4) != 0) {
        fprintf(stderr, "sps: '%s' is not a valid SPScript bytecode file\n", path);
        fclose(file);
        return false;
    }

    uint32_t version = 0;
    if (!read_u32(file, &version) || version != SPS_FILE_VERSION) {
        fprintf(stderr, "sps: unsupported bytecode version in '%s'\n", path);
        fclose(file);
        return false;
    }

    uint32_t proto_count = 0;
    if (!read_u32(file, &proto_count)) {
        fclose(file);
        return false;
    }

    for (uint32_t i = 0; i < proto_count; i++) {
        uint32_t name_len = 0;
        if (!read_u32(file, &name_len)) goto corrupted;
        char* name = arena_strndup(&program->arena, "", 0);
        if (name_len) {
            char* raw = (char*)malloc(name_len);
            if (!raw || fread(raw, 1, name_len, file) != name_len) { free(raw); goto corrupted; }
            name = arena_strndup(&program->arena, raw, name_len);
            free(raw);
        }

        FunctionProto* proto = (FunctionProto*)arena_calloc(&program->arena, 1, sizeof(FunctionProto));
        proto->name = name;

        int32_t arity = 0, upvalue_count = 0, stack_size = 0;
        if (!read_i32(file, &arity)) goto corrupted;
        if (!read_i32(file, &upvalue_count)) goto corrupted;
        if (!read_i32(file, &stack_size)) goto corrupted;
        proto->arity = arity;
        proto->upvalue_count = upvalue_count;
        proto->stack_size = stack_size;

        uint32_t code_len = 0;
        if (!read_u32(file, &code_len)) goto corrupted;
        proto->code = (Instr*)malloc(sizeof(Instr) * (size_t)(code_len ? code_len : 1));
        if (!proto->code) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        proto->code_len = (int)code_len;
        proto->code_cap = (int)code_len;
        for (uint32_t j = 0; j < code_len; j++) {
            Instr* instr = &proto->code[j];
            if (!read_u8(file, &instr->op)) goto corrupted;
            if (!read_u8(file, &instr->a)) goto corrupted;
            if (!read_u8(file, &instr->b)) goto corrupted;
            if (!read_u8(file, &instr->_pad)) goto corrupted;
            if (!read_i32(file, &instr->c)) goto corrupted;
            if (!read_i32(file, &instr->line)) goto corrupted;
        }

        uint32_t const_count = 0;
        if (!read_u32(file, &const_count)) goto corrupted;
        proto->constants = (Value*)malloc(sizeof(Value) * (size_t)(const_count ? const_count : 1));
        if (!proto->constants) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        proto->const_count = (int)const_count;
        proto->const_cap = (int)const_count;
        for (uint32_t j = 0; j < const_count; j++) {
            uint8_t tag = 0;
            if (!read_u8(file, &tag)) goto corrupted;
            switch (tag) {
                case CONST_TAG_NIL:
                    proto->constants[j] = value_nil();
                    break;
                case CONST_TAG_BOOL: {
                    uint8_t flag = 0;
                    if (!read_u8(file, &flag)) goto corrupted;
                    proto->constants[j] = value_bool(flag != 0);
                    break;
                }
                case CONST_TAG_INT: {
                    int64_t number = 0;
                    if (!read_i64(file, &number)) goto corrupted;
                    proto->constants[j] = value_int(number);
                    break;
                }
                case CONST_TAG_FLOAT: {
                    double number = 0.0;
                    if (!read_double(file, &number)) goto corrupted;
                    proto->constants[j] = value_float(number);
                    break;
                }
                case CONST_TAG_STRING: {
                    uint32_t length = 0;
                    if (!read_u32(file, &length)) goto corrupted;
                    char* raw = (char*)malloc(length + 1);
                    if (!raw) {
                        fprintf(stderr, "sps: out of memory\n");
                        exit(1);
                    }
                    if (length && fread(raw, 1, length, file) != length) {
                        free(raw);
                        goto corrupted;
                    }
                    raw[length] = '\0';
                    ObjString* str = string_new(vm, raw, length);
                    free(raw);
                    proto->constants[j] = value_obj(&str->obj);
                    break;
                }
                default:
                    goto corrupted;
            }
        }

        uint32_t upvalues = 0;
        if (!read_u32(file, &upvalues)) goto corrupted;
        proto->upvalues = (UpvalueDesc*)malloc(sizeof(UpvalueDesc) * (size_t)(upvalues ? upvalues : 1));
        if (!proto->upvalues) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        for (uint32_t j = 0; j < upvalues; j++) {
            int32_t index = 0;
            uint8_t is_local = 0;
            if (!read_i32(file, &index)) goto corrupted;
            if (!read_u8(file, &is_local)) goto corrupted;
            proto->upvalues[j].index = index;
            proto->upvalues[j].is_local = is_local != 0;
            proto->upvalues[j].name = NULL;
            proto->upvalues[j].name_len = 0;
        }

        program_add_proto(program, proto);
    }

    uint32_t class_count = 0;
    if (!read_u32(file, &class_count)) goto corrupted;
    for (uint32_t i = 0; i < class_count; i++) {
        uint32_t name_len = 0;
        if (!read_u32(file, &name_len)) goto corrupted;
        char* raw = (char*)malloc(name_len + 1);
        if (!raw) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        if (name_len && fread(raw, 1, name_len, file) != name_len) {
            free(raw);
            goto corrupted;
        }
        raw[name_len] = '\0';

        ClassProto* klass = (ClassProto*)arena_calloc(&program->arena, 1, sizeof(ClassProto));
        klass->name = arena_strndup(&program->arena, raw, name_len);
        free(raw);

        uint8_t is_struct = 0;
        if (!read_u8(file, &is_struct)) goto corrupted;
        klass->is_struct = is_struct != 0;

        uint32_t field_count = 0;
        if (!read_u32(file, &field_count)) goto corrupted;
        klass->field_count = field_count;
        klass->field_names = (char**)arena_calloc(&program->arena, sizeof(char*), field_count + 1);
        for (uint32_t j = 0; j < field_count; j++) {
            uint32_t len = 0;
            if (!read_u32(file, &len)) goto corrupted;
            char* item = (char*)malloc(len + 1);
            if (!item) {
                fprintf(stderr, "sps: out of memory\n");
                exit(1);
            }
            if (len && fread(item, 1, len, file) != len) {
                free(item);
                goto corrupted;
            }
            item[len] = '\0';
            klass->field_names[j] = arena_strndup(&program->arena, item, len);
            free(item);
        }

        uint32_t method_count = 0;
        if (!read_u32(file, &method_count)) goto corrupted;
        klass->method_count = method_count;
        klass->method_names = (char**)arena_calloc(&program->arena, sizeof(char*), method_count + 1);
        klass->method_protos = (int*)arena_calloc(&program->arena, sizeof(int), method_count + 1);
        for (uint32_t j = 0; j < method_count; j++) {
            uint32_t len = 0;
            if (!read_u32(file, &len)) goto corrupted;
            char* item = (char*)malloc(len + 1);
            if (!item) {
                fprintf(stderr, "sps: out of memory\n");
                exit(1);
            }
            if (len && fread(item, 1, len, file) != len) {
                free(item);
                goto corrupted;
            }
            item[len] = '\0';
            klass->method_names[j] = arena_strndup(&program->arena, item, len);
            free(item);
            int32_t proto_index = 0;
            if (!read_i32(file, &proto_index)) goto corrupted;
            klass->method_protos[j] = proto_index;
        }

        int32_t init_proto = -1;
        if (!read_i32(file, &init_proto)) goto corrupted;
        klass->init_proto = init_proto;

        program_add_class(program, klass);
    }

    uint32_t global_count = 0;
    if (!read_u32(file, &global_count)) goto corrupted;
    for (uint32_t i = 0; i < global_count; i++) {
        uint32_t length = 0;
        if (!read_u32(file, &length)) goto corrupted;
        char* name = (char*)malloc(length + 1);
        if (!name) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        if (length && fread(name, 1, length, file) != length) {
            free(name);
            goto corrupted;
        }
        name[length] = '\0';
        program_add_global_name(program, name, (int)length);
        free(name);
    }

    uint32_t entry = 0;
    if (!read_u32(file, &entry)) goto corrupted;
    program->entry_proto = (int)entry;

    fclose(file);
    return true;

corrupted:
    fclose(file);
    fprintf(stderr, "sps: '%s' is corrupted\n", path);
    return false;
}
