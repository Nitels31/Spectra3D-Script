// Spectra3D Script - the context of the public API

#include "api_internal.h"

#include <stdio.h>

// The conversion between the values of the core and the values of the API.
SpsValue sps_internal_to_api(Value value) {
    SpsValue result;
    result.type = SPS_NIL;
    result.as.integer = 0;

    switch (value.type) {
        case VAL_NIL:      result.type = SPS_NIL; break;
        case VAL_BOOL:     result.type = SPS_BOOL; result.as.boolean = value.as.boolean; break;
        case VAL_INT:      result.type = SPS_INT; result.as.integer = value.as.integer; break;
        case VAL_FLOAT:
        case VAL_FLOAT32:  result.type = SPS_FLOAT; result.as.number = value.as.number; break;
        case VAL_STRING: {
            ObjString* text = (ObjString*)value.as.obj;
            result.type = SPS_STRING;
            result.as.string = text->chars;
            break;
        }
        case VAL_LIST:     result.type = SPS_LIST; result.as.pointer = value.as.obj; break;
        case VAL_DICT:     result.type = SPS_DICT; result.as.pointer = value.as.obj; break;
        case VAL_OBJECT:   result.type = SPS_OBJECT; result.as.pointer = value.as.obj; break;
        case VAL_CLASS:    result.type = SPS_CLASS; result.as.pointer = value.as.obj; break;
        case VAL_USERDATA: result.type = SPS_USERDATA; result.as.pointer = value.as.obj; break;
        case VAL_HOSTCLASS: result.type = SPS_CLASS; result.as.pointer = value.as.pointer; break;
        case VAL_CLOSURE:
        case VAL_NATIVE:   result.type = SPS_FUNCTION; result.as.pointer = value.as.obj; break;
    }
    return result;
}

// A temporary string of the pool: the content of the text is copied into it.
static ObjString* scratch_string(SpsContext* context, const char* text) {
    ObjString* scratch = context->scratch[context->scratch_index];
    context->scratch_index = (context->scratch_index + 1) % SPS_SCRATCH_COUNT;

    size_t length = text ? strlen(text) : 0;
    if (length + 1 > scratch->len + 1) {
        // the text is too long for the pool: it gets its own string
        ObjString* own = (ObjString*)malloc(sizeof(ObjString) + length + 1);
        if (!own) return scratch;
        own->obj.type = OBJ_STRING;
        own->obj.next = context->vm->objects;
        context->vm->objects = &own->obj;
        own->len = (uint32_t)length;
        memcpy(own->chars, text ? text : "", length);
        own->chars[length] = '\0';
        own->hash = string_hash(own->chars, length);
        return own;
    }

    scratch->len = (uint32_t)length;
    memcpy(scratch->chars, text ? text : "", length);
    scratch->chars[length] = '\0';
    scratch->hash = string_hash(scratch->chars, length);
    return scratch;
}

// The result of a native function: the text is copied into the heap of the
// virtual machine, so the host may return a temporary buffer.
Value sps_internal_result_from_api(SpsContext* context, SpsValue value) {
    if (value.type != SPS_STRING) return sps_internal_from_api(context, value);

    ObjString* text = string_new_cstr(context->vm, value.as.string);
    Value result;
    result.type = VAL_STRING;
    result.as.obj = &text->obj;
    return result;
}

const char* sps_internal_scratch(SpsContext* context, const char* text) {
    return scratch_string(context, text)->chars;
}

Value sps_internal_from_api(SpsContext* context, SpsValue value) {
    Value result;
    result.type = VAL_NIL;
    result.as.integer = 0;

    switch (value.type) {
        case SPS_NIL:      result.type = VAL_NIL; break;
        case SPS_BOOL:     result.type = VAL_BOOL; result.as.boolean = value.as.boolean; break;
        case SPS_INT:      result.type = VAL_INT; result.as.integer = value.as.integer; break;
        case SPS_FLOAT:    result.type = VAL_FLOAT; result.as.number = value.as.number; break;
        case SPS_STRING: {
            result.type = VAL_STRING;
            result.as.obj = &scratch_string(context, value.as.string)->obj;
            break;
        }
        case SPS_LIST:
        case SPS_DICT:
        case SPS_OBJECT:
        case SPS_USERDATA:
            result.type = value.type == SPS_LIST ? VAL_LIST
                       : value.type == SPS_DICT ? VAL_DICT
                       : value.type == SPS_OBJECT ? VAL_OBJECT : VAL_USERDATA;
            result.as.obj = (Obj*)value.as.pointer;
            break;
        case SPS_CLASS:    result.type = VAL_CLASS; result.as.obj = (Obj*)value.as.pointer; break;
        case SPS_FUNCTION: result.type = VAL_CLOSURE; result.as.obj = (Obj*)value.as.pointer; break;
    }
    return result;
}

void sps_internal_set_error(SpsContext* context, const char* message) {
    if (!context) return;
    snprintf(context->last_error, sizeof(context->last_error), "%s", message ? message : "");
}

// The handler of the virtual machine: it fills the context and stops the run.
static void vm_error_handler(VM* vm, const char* message, void* user) {
    SpsContext* context = (SpsContext*)user;
    if (!context) return;

    snprintf(context->last_error, sizeof(context->last_error), "%s", message ? message : "");
    context->last_stack[0] = '\0';

    if (context->error_handler) {
        context->error_handler(context, context->last_error, context->last_stack, context->error_user);
    }
}

SpsContext* sps_context_new(void) {
    SpsContext* context = (SpsContext*)calloc(1, sizeof(SpsContext));
    if (!context) return NULL;

    Program program;
    program_init(&program);
    context->vm = vm_new(&program);
    context->vm->error_handler = vm_error_handler;
    context->vm->error_user = context;
    context->vm->has_error = false;

    for (int i = 0; i < SPS_SCRATCH_COUNT; i++) {
        context->scratch[i] = (ObjString*)malloc(sizeof(ObjString) + SPS_SCRATCH_SIZE);
        if (!context->scratch[i]) {
            free(context);
            return NULL;
        }
        memset(context->scratch[i], 0, sizeof(ObjString));
        context->scratch[i]->obj.type = OBJ_STRING;
        context->scratch[i]->len = SPS_SCRATCH_SIZE - 1;
    }
    return context;
}

void sps_context_free(SpsContext* context) {
    if (!context) return;

    for (int i = 0; i < context->chunk_count; i++) {
        program_release(&context->chunks[i]->program);
        free(context->chunks[i]->name);
        free(context->chunks[i]);
    }
    free(context->chunks);
    free(context->diagnostics);

    vm_free(context->vm);
    free(context);
}

void sps_context_set_error_handler(SpsContext* context, SpsErrorHandler handler, void* user) {
    if (!context) return;
    context->error_handler = handler;
    context->error_user = user;
}

const char* sps_context_last_error(SpsContext* context) {
    if (!context) return "";
    return context->last_error;
}

// Prepares the virtual machine to run a program.
static void prepare_vm(SpsContext* context, Program* program) {
    context->vm->program = program;
    context->vm->has_error = false;
    context->last_error[0] = '\0';
    vm_init_globals(context->vm);
}

bool sps_context_exec(SpsContext* context, const char* source, const char* chunk_name) {
    if (!context || !source) return false;

    SpsChunk* chunk = sps_compile(context, source, chunk_name ? chunk_name : "<string>");
    if (!chunk) return false;

    prepare_vm(context, &chunk->program);
    vm_run(context->vm);
    return !context->vm->has_error;
}

bool sps_context_exec_file(SpsContext* context, const char* path) {
    if (!context || !path) return false;

    FILE* file = fopen(path, "rb");
    if (!file) {
        sps_internal_set_error(context, "cannot open the file");
        return false;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) size = 0;
    char* source = (char*)malloc((size_t)size + 1);
    if (!source) {
        fclose(file);
        sps_internal_set_error(context, "out of memory");
        return false;
    }
    size_t read = fread(source, 1, (size_t)size, file);
    fclose(file);
    source[read] = '\0';

    bool result = sps_context_exec(context, source, path);
    free(source);
    return result;
}

bool sps_context_run(SpsContext* context, SpsChunk* chunk) {
    if (!context || !chunk) return false;
    prepare_vm(context, &chunk->program);
    vm_run(context->vm);
    return !context->vm->has_error;
}

SpsValue sps_context_get_global(SpsContext* context, const char* name) {
    if (!context || !name) return sps_nil();

    for (int i = 0; i < context->vm->global_count; i++) {
        const char* global_name = context->vm->program->global_names[i];
        if (global_name && strcmp(global_name, name) == 0) {
            return sps_internal_to_api(context->vm->globals[i]);
        }
    }
    return sps_nil();
}

void sps_context_set_global(SpsContext* context, const char* name, SpsValue value) {
    if (!context || !name) return;

    Program* program = context->vm->program;
    for (int i = 0; i < context->vm->global_count; i++) {
        const char* global_name = program->global_names[i];
        if (global_name && strcmp(global_name, name) == 0) {
            context->vm->globals[i] = sps_internal_from_api(context, value);
            return;
        }
    }
    sps_internal_set_error(context, "there is no such global variable");
}

bool sps_context_has_global(SpsContext* context, const char* name) {
    if (!context || !name) return false;
    Program* program = context->vm->program;
    for (int i = 0; i < context->vm->global_count; i++) {
        const char* global_name = program->global_names[i];
        if (global_name && strcmp(global_name, name) == 0) return true;
    }
    return false;
}

bool sps_context_clear_global(SpsContext* context, const char* name) {
    if (!context || !name) return false;
    Program* program = context->vm->program;
    for (int i = 0; i < context->vm->global_count; i++) {
        const char* global_name = program->global_names[i];
        if (global_name && strcmp(global_name, name) == 0) {
            context->vm->globals[i] = value_nil();
            return true;
        }
    }
    return false;
}

int sps_context_global_count(SpsContext* context) {
    return context ? context->vm->global_count : 0;
}

const char* sps_context_global_name(SpsContext* context, int index) {
    if (!context || index < 0 || index >= context->vm->global_count) return "";
    const char* name = context->vm->program->global_names[index];
    return name ? name : "";
}
