// Spectra3D Script - virtual machine (stack based bytecode interpreter)
#include "vm.h"
#include "builtins.h"
#include "dict.h"
#include "methods.h"
#include "spslib.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void close_upvalues_from(VM* vm, Value* location);

// Runs the bytecode until the number of frames drops to stop_frame. The host
// uses it to call a function of the language from the outside.
static bool vm_execute(VM* vm, int stop_frame);

static Value native_value(NativeEntry* entry) {
    Value value;
    value.type = VAL_NATIVE;
    value.as.native = entry;
    return value;
}

static void* xmalloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    return ptr;
}

// ------------------------------------------------------------------
// the native functions
// ------------------------------------------------------------------

int vm_register_native(VM* vm, const char* name, NativeFn fn, void* user) {
    int index = vm_find_native(vm, name);
    if (index >= 0) {
        vm->natives[index].fn = fn;
        vm->natives[index].user = user;
        return index;
    }
    if (vm->native_count == vm->native_cap) {
        int capacity = vm->native_cap ? vm->native_cap * 2 : 32;
        vm->natives = (NativeBinding*)realloc(vm->natives, sizeof(NativeBinding) * (size_t)capacity);
        if (!vm->natives) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        vm->native_cap = capacity;
    }
    NativeBinding* binding = &vm->natives[vm->native_count];
    binding->name = strdup(name);
    binding->fn = fn;
    binding->user = user;
    binding->entry = (NativeEntry*)malloc(sizeof(NativeEntry));
    if (!binding->entry) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    binding->entry->fn = fn;
    binding->entry->user = user;
    return vm->native_count++;
}

int vm_register_host_class(VM* vm, const char* name) {
    for (int i = 0; i < vm->host_class_count; i++) {
        if (strcmp(vm->host_classes[i]->name, name) == 0) return i;
    }
    if (vm->host_class_count == vm->host_class_cap) {
        int capacity = vm->host_class_cap ? vm->host_class_cap * 2 : 8;
        vm->host_classes = (HostClass**)realloc(vm->host_classes,
                                                 sizeof(HostClass*) * (size_t)capacity);
        if (!vm->host_classes) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        vm->host_class_cap = capacity;
    }
    vm->host_classes[vm->host_class_count] = host_class_new(name);
    return vm->host_class_count++;
}

static Value host_class_value(HostClass* klass) {
    Value value;
    value.type = VAL_HOSTCLASS;
    value.as.pointer = klass;
    return value;
}

int vm_find_native(VM* vm, const char* name) {
    for (int i = 0; i < vm->native_count; i++) {
        if (strcmp(vm->natives[i].name, name) == 0) return i;
    }
    return -1;
}

// ------------------------------------------------------------------
// lifecycle
// ------------------------------------------------------------------

// ------------------------------------------------------------------
// the cache of the method lookups
// ------------------------------------------------------------------

#define METHOD_CACHE_SIZE 512

// The method lookup walks the methods of the class and compares the names.
// The cache remembers the result of the last lookups, so a call of a method
// of a hot class costs one hash instead of the scan.
static uint32_t method_cache_hash(void* klass, const char* name) {
    uint32_t hash = 2166136261u;
    uintptr_t key = (uintptr_t)klass;
    for (int i = 0; i < (int)sizeof(uintptr_t); i++) {
        hash ^= (unsigned char)(key >> (i * 8));
        hash *= 16777619u;
    }
    for (const char* p = name; *p; p++) {
        hash ^= (unsigned char)*p;
        hash *= 16777619u;
    }
    return hash;
}

static int vm_find_method(VM* vm, ObjClass* klass, const char* name) {
    if (vm->method_cache == NULL) {
        vm->method_cache = (MethodCacheEntry*)xmalloc(sizeof(MethodCacheEntry) * METHOD_CACHE_SIZE);
        memset(vm->method_cache, 0, sizeof(MethodCacheEntry) * METHOD_CACHE_SIZE);
        vm->method_cache_size = METHOD_CACHE_SIZE;
    }

    uint32_t slot = method_cache_hash(klass, name) & (uint32_t)(METHOD_CACHE_SIZE - 1);
    MethodCacheEntry* entry = &vm->method_cache[slot];
    if (entry->klass == (void*)klass && entry->name != NULL && strcmp(entry->name, name) == 0) {
        return entry->index;
    }

    int index = class_find_method(klass, name);
    if (index >= 0) {
        entry->klass = (void*)klass;
        entry->name = name;
        entry->index = index;
    }
    return index;
}

VM* vm_new(Program* program) {
    VM* vm = (VM*)xmalloc(sizeof(VM));
    memset(vm, 0, sizeof(VM));

    vm->program = program;
    vm->stack = (Value*)xmalloc(sizeof(Value) * VM_STACK_MAX);
    vm->frame_arena = (Value*)xmalloc(sizeof(Value) * VM_FRAME_ARENA_MAX);
    vm->frames = (CallFrame*)xmalloc(sizeof(CallFrame) * VM_FRAME_MAX);
    vm->handlers = (ErrorHandler*)xmalloc(sizeof(ErrorHandler) * VM_HANDLER_MAX);
    vm->handler_cap = VM_HANDLER_MAX;
    vm->handler_count = 0;

    vm->arena_top = 0;
    vm->global_count = 0;
    vm->globals = NULL;

    vm->bytes_allocated = 0;
    vm->object_count = 0;
    vm->exit_code = 0;

    for (int i = 0; i < SPS_BUILTIN_COUNT; i++) {
        vm_register_native(vm, SPS_BUILTINS[i].name, SPS_BUILTINS[i].fn, NULL);
    }
    return vm;
}

void vm_init_globals(VM* vm) {
    free(vm->globals);
    vm->global_count = vm->program->global_count;
    size_t count = vm->global_count > 0 ? (size_t)vm->global_count : 1;
    vm->globals = (Value*)xmalloc(sizeof(Value) * count);
    for (int i = 0; i < vm->global_count; i++) vm->globals[i] = value_nil();
    int natives = vm->native_count;
    if (natives > vm->global_count) natives = vm->global_count;
    for (int i = 0; i < natives; i++) {
        vm->globals[i] = native_value(vm->natives[i].entry);
    }
    for (int i = 0; i < vm->host_class_count; i++) {
        int index = natives + i;
        if (index < vm->global_count) vm->globals[index] = host_class_value(vm->host_classes[i]);
    }
}

void vm_free(VM* vm) {
    if (!vm) return;
    objects_free_all(vm);
    free(vm->stack);
    free(vm->frame_arena);
    free(vm->frames);
    free(vm->handlers);
    free(vm->globals);
    for (int i = 0; i < vm->native_count; i++) {
        free(vm->natives[i].name);
        free(vm->natives[i].entry);
    }
    free(vm->natives);
    for (int i = 0; i < vm->host_class_count; i++) {
        HostClass* klass = vm->host_classes[i];
        for (int j = 0; j < klass->method_count; j++) free(klass->methods[j].name);
        free(klass->methods);
        free(klass->name);
        free(klass);
    }
    free(vm->host_classes);
    free(vm);
}

// ------------------------------------------------------------------
// errors
// ------------------------------------------------------------------

void vm_runtime_error(VM* vm, int line, const char* fmt, ...) {
    char message[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    if (vm->handler_count > 0) {
        // the error is caught by a try block: restore the state and continue
        ErrorHandler* handler = &vm->handlers[--vm->handler_count];

        close_upvalues_from(vm, &vm->frame_arena[handler->arena_top]);
        vm->frame_count = handler->frame_count;
        vm->stack_top = handler->stack_top;
        vm->arena_top = handler->arena_top;

        ObjString* text = string_new(vm, message, strlen(message));
        vm->stack[vm->stack_top++] = value_obj(&text->obj);

        CallFrame* target = &vm->frames[vm->frame_count - 1];
        vm->error_ip = target->closure->proto->code + handler->handler_offset;
        longjmp(*vm->error_jump, 1);
    }

    if (vm->error_handler) {
        // the host wants to handle the error itself
        vm->has_error = true;
        vm->error_handler(vm, message, vm->error_user);
        longjmp(*vm->error_jump, 2);
    }

    fflush(stdout);
    fprintf(stderr, "%s:%d: error: %s\n", vm->script_name ? vm->script_name : "<script>",
            line, message);

    for (int i = vm->frame_count - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        fprintf(stderr, "  in %s (line %d)\n",
                frame->closure && frame->closure->proto->name ? frame->closure->proto->name : "?",
                frame->line);
    }
    fflush(stderr);
    vm->exit_code = 1;
    exit(1);
}

void vm_print_stats(VM* vm) {
    fprintf(stderr, "[sps] objects: %zu, allocated: %zu bytes\n", vm->object_count,
            vm->bytes_allocated);
}

// ------------------------------------------------------------------
// upvalues
// ------------------------------------------------------------------

static ObjUpvalue* capture_upvalue(VM* vm, Value* location) {
    ObjUpvalue* previous = NULL;
    ObjUpvalue* current = vm->open_upvalues;

    while (current != NULL && current->location > location) {
        previous = current;
        current = current->next;
    }
    if (current != NULL && current->location == location) return current;

    ObjUpvalue* created = (ObjUpvalue*)xmalloc(sizeof(ObjUpvalue));
    created->obj.type = OBJ_UPVALUE;
    created->obj.next = vm->objects;
    vm->objects = &created->obj;
    created->location = location;
    created->closed = value_nil();
    created->is_closed = false;
    created->next = current;
    if (previous) previous->next = created;
    else vm->open_upvalues = created;
    mem_account(vm, sizeof(ObjUpvalue));
    return created;
}

static void close_upvalues_from(VM* vm, Value* location) {
    while (vm->open_upvalues != NULL && vm->open_upvalues->location >= location) {
        ObjUpvalue* upvalue = vm->open_upvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = NULL;
        upvalue->is_closed = true;
        vm->open_upvalues = upvalue->next;
    }
}

// ------------------------------------------------------------------
// frames
// ------------------------------------------------------------------

static void vm_enter(VM* vm, ObjClosure* closure, Value* args, int argc, int line,
                     int stack_base) {
    FunctionProto* proto = closure->proto;
    if (vm->frame_count >= VM_FRAME_MAX) {
        vm_runtime_error(vm, line, "stack overflow: too many nested calls");
    }
    if (vm->arena_top + (size_t)proto->stack_size > (size_t)VM_FRAME_ARENA_MAX) {
        vm_runtime_error(vm, line, "stack overflow: too many local variables");
    }

    // the local slots are taken from the frame arena - no initialisation is
    // needed because the compiler always writes a slot before it is read
    // the local slots are taken from the frame arena. They are not cleared:
    // the compiler always writes a slot before it reads it, so the old values
    // of the frame are never visible.
    Value* slots = &vm->frame_arena[vm->arena_top];
    vm->arena_top += (size_t)proto->stack_size;
    for (int i = 0; i < argc; i++) slots[i] = args[i];

    CallFrame* frame = &vm->frames[vm->frame_count++];
    frame->closure = closure;
    frame->ip = proto->code;
    frame->slots = slots;
    frame->slot_base = (size_t)(slots - vm->frame_arena);
    frame->stack_base = stack_base;
    frame->line = 0;
    frame->discards_result = false;
    frame->handler_base = vm->handler_count;
}

// ------------------------------------------------------------------
// index helpers
// ------------------------------------------------------------------

static int64_t require_index(VM* vm, Value index, int line) {
    if (index.type != VAL_INT) {
        vm_runtime_error(vm, line, "index must be an integer");
    }
    return index.as.integer;
}

static Value get_index(VM* vm, Value container, Value index, int line) {
    if (container.type == VAL_DICT) {
        ObjDict* dict = (ObjDict*)container.as.obj;
        Value value;
        if (!dict_get(dict, index, &value)) {
            vm_runtime_error(vm, line, "key not found in the dictionary");
        }
        return value;
    }

    int64_t position = require_index(vm, index, line);

    if (container.type == VAL_STRING) {
        ObjString* str = (ObjString*)container.as.obj;
        if (position < 0) position += (int64_t)str->len;
        if (position < 0 || position >= (int64_t)str->len) {
            vm_runtime_error(vm, line, "string index out of range");
        }
        ObjString* part = string_new(vm, str->chars + position, 1);
        return value_obj(&part->obj);
    }

    if (container.type == VAL_LIST) {
        ObjList* list = (ObjList*)container.as.obj;
        if (position < 0) position += (int64_t)list->count;
        if (position < 0 || position >= (int64_t)list->count) {
            vm_runtime_error(vm, line, "list index out of range");
        }
        return list->items[position];
    }

    vm_runtime_error(vm, line, "value is not indexable");
    return value_nil();
}

static void set_index(VM* vm, Value container, Value index, Value value, int line) {
    if (container.type == VAL_DICT) {
        ObjDict* dict = (ObjDict*)container.as.obj;
        dict_set(vm, dict, index, value);
        return;
    }

    int64_t position = require_index(vm, index, line);

    if (container.type == VAL_LIST) {
        ObjList* list = (ObjList*)container.as.obj;
        if (position < 0) position += (int64_t)list->count;
        if (position < 0 || position >= (int64_t)list->count) {
            vm_runtime_error(vm, line, "list index out of range");
        }
        list->items[position] = value;
        return;
    }

    if (container.type == VAL_STRING) {
        vm_runtime_error(vm, line, "strings are immutable");
    }

    vm_runtime_error(vm, line, "value does not support item assignment");
}

// ------------------------------------------------------------------
// arithmetic helpers
// ------------------------------------------------------------------

static double to_double(Value value) {
    return value.type == VAL_INT ? (double)value.as.integer : value.as.number;
}

// C++ like promotion: float + double -> double, int + float -> float
static ValueType promote(ValueType a, ValueType b) {
    if (a == VAL_FLOAT || b == VAL_FLOAT) return VAL_FLOAT;
    if (a == VAL_FLOAT32 || b == VAL_FLOAT32) return VAL_FLOAT32;
    return VAL_INT;
}

static Value make_number(ValueType type, double value) {
    return type == VAL_FLOAT32 ? value_float32(value) : value_float(value);
}

// a struct is copied on assignment, a class keeps the reference
static Value copy_if_struct(VM* vm, Value value) {
    if (value.type == VAL_OBJECT) {
        ObjInstance* instance = (ObjInstance*)value.as.obj;
        if (instance->klass->is_struct) {
            return value_obj(&instance_copy(vm, instance)->obj);
        }
    }
    return value;
}

static void cast_error(VM* vm, int line, ValueType target, Value value) {
    vm_runtime_error(vm, line, "expected '%s', got '%s'", value_type_name(target),
                     value_type_name(value.type));
}

static Value string_repeat(VM* vm, ObjString* str, int64_t times, int line) {
    if (times < 0) vm_runtime_error(vm, line, "negative repeat count");
    size_t length = (size_t)str->len * (size_t)times;
    char* buffer = (char*)xmalloc(length + 1);
    for (int64_t i = 0; i < times; i++) {
        memcpy(buffer + (size_t)i * str->len, str->chars, str->len);
    }
    ObjString* result = string_new(vm, buffer, length);
    free(buffer);
    return value_obj(&result->obj);
}

// Calls a function of the language from the outside of the virtual machine.
bool vm_call_value(VM* vm, Value callee, int argc, Value* args, Value* out) {
    if (callee.type == VAL_NATIVE) {
        Value result = callee.as.native->fn(vm, argc, args, callee.as.native->user);
        if (out) *out = result;
        return true;
    }
    if (callee.type != VAL_CLOSURE) return false;

    ObjClosure* closure = (ObjClosure*)callee.as.obj;
    FunctionProto* proto = closure->proto;
    if (proto->arity != argc) return false;
    if (vm->frame_count >= VM_FRAME_MAX) return false;
    if (vm->arena_top + (size_t)proto->stack_size > (size_t)VM_FRAME_ARENA_MAX) return false;

    int base = vm->stack_top;
    vm->stack[vm->stack_top++] = callee;
    for (int i = 0; i < argc; i++) vm->stack[vm->stack_top++] = args[i];

    Value* slots = &vm->frame_arena[vm->arena_top];
    vm->arena_top += (size_t)proto->stack_size;
    for (int i = 0; i < proto->stack_size; i++) slots[i] = value_nil();
    for (int i = 0; i < argc; i++) slots[i] = vm->stack[base + 1 + i];

    CallFrame* frame = &vm->frames[vm->frame_count++];
    frame->closure = closure;
    frame->ip = proto->code;
    frame->slots = slots;
    frame->slot_base = (size_t)(slots - vm->frame_arena);
    frame->stack_base = base;
    frame->line = 0;
    frame->discards_result = false;
    frame->handler_base = vm->handler_count;

    int stop = vm->frame_count - 1;
    vm_execute(vm, stop);

    // the result of the call is in the slot of the callee
    if (out) *out = vm->stack[frame->stack_base];
    vm->stack_top = base;
    return !vm->has_error;
}

// ------------------------------------------------------------------
// the interpreter
// ------------------------------------------------------------------

Value vm_run(VM* vm) {
    FunctionProto* entry = vm->program->protos[vm->program->entry_proto];
    ObjClosure* closure = closure_new(vm, entry);
    vm_enter(vm, closure, NULL, 0, 0, 0);
    vm_execute(vm, 0);
    return value_nil();
}

static bool vm_execute(VM* vm, int stop_frame) {

CallFrame* frame = &vm->frames[vm->frame_count - 1];
    Instr* ip = frame->ip;
    Value* stack = vm->stack; // hot values are kept in local variables
    int sp = vm->stack_top;

    Instr instr;

    static const void* dispatch_table[] = {
        &&op_FORMAT,
        &&op_IMPORT,
        &&op_TRY,
        &&op_TRY_END,
        &&op_CLASS,
        &&op_GET_FIELD,
        &&op_SET_FIELD,
        &&op_CAST,
        &&op_CONST,
        &&op_NIL,
        &&op_TRUE,
        &&op_FALSE,
        &&op_POP,
        &&op_DUP,
        &&op_GET_LOCAL,
        &&op_GET_LOCAL_CONST,
        &&op_GET_GLOBAL_CONST,
        &&op_GET_LOCAL_ADD,
        &&op_ADD_CONST,
        &&op_SET_LOCAL,
        &&op_GET_GLOBAL,
        &&op_SET_GLOBAL,
        &&op_GET_UPVAL,
        &&op_SET_UPVAL,
        &&op_NATIVE,
        &&op_ADD,
        &&op_SUB,
        &&op_MUL,
        &&op_DIV,
        &&op_MOD,
        &&op_NEG,
        &&op_NOT,
        &&op_IN,
        &&op_EQ,
        &&op_NE,
        &&op_LT,
        &&op_LE,
        &&op_GT,
        &&op_GE,
        &&op_BITAND,
        &&op_BITOR,
        &&op_BITXOR,
        &&op_SHL,
        &&op_SHR,
        &&op_JMP,
        &&op_JMP_IF_FALSE,
        &&op_JMP_IF_TRUE,
        &&op_CALL,
        &&op_METHOD,
        &&op_METHOD_NAME,
        &&op_ITER_INIT,
        &&op_ITER_NEXT,
        &&op_RETURN,
        &&op_CLOSURE,
        &&op_CLOSE_UPVAL,
        &&op_LIST,
        &&op_DICT,
        &&op_GET_INDEX,
        &&op_GET_INDEX_KEEP,
        &&op_SET_INDEX,
        &&op_LEN,
        &&op_default,
    };

#define DISPATCH() do { \
        instr = *ip++; \
        if (instr.line != frame->line) frame->line = instr.line; \
        if (instr.op >= OP_COUNT) goto op_default; \
        goto *dispatch_table[instr.op]; \
    } while (0)

    jmp_buf jump;
    jmp_buf* saved_jump = vm->error_jump;
    vm->error_jump = &jump;

    int error_state = setjmp(jump);
    if (error_state != 0) {
        if (error_state == 2) {
            // an uncaught error, the host handler was called
            vm->error_jump = saved_jump;
            return false;
        }
        // the error was caught by a try block, continue at the handler
        frame = &vm->frames[vm->frame_count - 1];
        ip = vm->error_ip;
        sp = vm->stack_top;
    }

    DISPATCH();
            op_FORMAT: {
                Value value = stack[sp - 1];
                const char* spec = "";
                int spec_length = 0;
                if (instr.c >= 0) {
                    ObjString* text = (ObjString*)frame->closure->proto->constants[instr.c].as.obj;
                    spec = text->chars;
                    spec_length = text->len;
                }
                ObjString* result = sps_format_value(vm, value, spec, spec_length);
                stack[sp - 1] = value_obj(&result->obj);
                DISPATCH();
            }

            op_IMPORT: {
                // run the top level code of a module
                FunctionProto* proto = vm->program->protos[instr.c];
                ObjClosure* module = closure_new(vm, proto);
                frame->ip = ip; // remember the return address
                vm_enter(vm, module, NULL, 0, instr.line, sp);
                vm->frames[vm->frame_count - 1].discards_result = true;
                frame = &vm->frames[vm->frame_count - 1];
                ip = frame->ip;
                DISPATCH();
            }

            op_TRY: {
                if (vm->handler_count >= vm->handler_cap) {
                    vm_runtime_error(vm, instr.line, "too many nested try blocks");
                }
                ErrorHandler* handler = &vm->handlers[vm->handler_count++];
                handler->handler_offset = instr.c;
                handler->frame_count = vm->frame_count;
                handler->stack_top = sp;
                handler->arena_top = vm->arena_top;
                DISPATCH();
            }

            op_TRY_END:
                if (vm->handler_count > 0) vm->handler_count--;
                DISPATCH();

            op_CLASS: {
                ClassProto* proto = vm->program->classes[instr.c];
                ObjClass* klass = class_new(vm, proto->name, proto->is_struct);

                klass->field_count = proto->field_count;
                klass->field_names = (char**)xmalloc(sizeof(char*) * (proto->field_count + 1));
                for (uint32_t i = 0; i < proto->field_count; i++) {
                    klass->field_names[i] = strdup(proto->field_names[i]);
                }

                klass->method_count = proto->method_count;
                klass->method_names = (char**)xmalloc(sizeof(char*) * (proto->method_count + 1));
                klass->methods = (ObjClosure**)xmalloc(sizeof(ObjClosure*) * (proto->method_count + 1));
                for (uint32_t i = 0; i < proto->method_count; i++) {
                    klass->method_names[i] = strdup(proto->method_names[i]);
                    FunctionProto* method_proto = vm->program->protos[proto->method_protos[i]];
                    ObjClosure* method = closure_new(vm, method_proto);
                    for (int j = 0; j < method_proto->upvalue_count; j++) {
                        UpvalueDesc* desc = &method_proto->upvalues[j];
                        if (desc->is_local) {
                            method->upvalues[j] = capture_upvalue(vm, frame->slots + desc->index);
                        } else {
                            method->upvalues[j] = frame->closure->upvalues[desc->index];
                        }
                    }
                    klass->methods[i] = method;
                }

                if (proto->init_proto >= 0) {
                    FunctionProto* init_proto = vm->program->protos[proto->init_proto];
                    ObjClosure* init = closure_new(vm, init_proto);
                    for (int j = 0; j < init_proto->upvalue_count; j++) {
                        UpvalueDesc* desc = &init_proto->upvalues[j];
                        if (desc->is_local) {
                            init->upvalues[j] = capture_upvalue(vm, frame->slots + desc->index);
                        } else {
                            init->upvalues[j] = frame->closure->upvalues[desc->index];
                        }
                    }
                    klass->init = init;
                }

                stack[sp++] = value_obj(&klass->obj);
                DISPATCH();
            }

            op_GET_FIELD: {
                Value object = stack[sp - 1];
                ObjString* name = (ObjString*)frame->closure->proto->constants[instr.c].as.obj;
                if (object.type == VAL_USERDATA) {
                    // a property of a class of the host
                    ObjUserdata* host = (ObjUserdata*)object.as.obj;
                    int index = host_class_find_property(host->klass, name->chars);
                    if (index < 0) {
                        vm_runtime_error(vm, instr.line, "class '%s' has no property '%s'",
                                         host->klass->name, name->chars);
                    }
                    HostProperty* property = &host->klass->properties[index];
                    if (!property->getter) {
                        vm_runtime_error(vm, instr.line, "property '%s' is read only", name->chars);
                    }
                    stack[sp - 1] = property->getter(vm, 1, &object, property->user);
                    DISPATCH();
                }
                if (object.type != VAL_OBJECT) {
                    vm_runtime_error(vm, instr.line, "value has no field '%s'", name->chars);
                }
                ObjInstance* instance = (ObjInstance*)object.as.obj;
                int index = class_find_field(instance->klass, name->chars);
                if (index < 0) {
                    vm_runtime_error(vm, instr.line, "class '%s' has no field '%s'",
                                     instance->klass->name, name->chars);
                }
                stack[sp - 1] = instance->fields[index];
                DISPATCH();
            }

            op_SET_FIELD: {
                Value value = stack[sp - 1];
                Value object = stack[sp - 2];
                ObjString* name = (ObjString*)frame->closure->proto->constants[instr.c].as.obj;
                if (object.type == VAL_USERDATA) {
                    // a property of a class of the host
                    ObjUserdata* host = (ObjUserdata*)object.as.obj;
                    int index = host_class_find_property(host->klass, name->chars);
                    if (index < 0) {
                        vm_runtime_error(vm, instr.line, "class '%s' has no property '%s'",
                                         host->klass->name, name->chars);
                    }
                    HostProperty* property = &host->klass->properties[index];
                    if (!property->setter) {
                        vm_runtime_error(vm, instr.line, "property '%s' is read only", name->chars);
                    }
                    Value arguments[2];
                    arguments[0] = object;
                    arguments[1] = value;
                    property->setter(vm, 2, arguments, property->user);
                    sp--; // the value stays on the stack
                    DISPATCH();
                }
                if (object.type != VAL_OBJECT) {
                    vm_runtime_error(vm, instr.line, "value has no field '%s'", name->chars);
                }
                ObjInstance* instance = (ObjInstance*)object.as.obj;
                int index = class_find_field(instance->klass, name->chars);
                if (index < 0) {
                    vm_runtime_error(vm, instr.line, "class '%s' has no field '%s'",
                                     instance->klass->name, name->chars);
                }
                instance->fields[index] = value;
                sp--; // drop the object, keep the value
                DISPATCH();
            }

            op_CAST: {
                Value value = stack[sp - 1];
                ValueType target = (ValueType)instr.c;
                switch (target) {
                    case VAL_INT:
                        if (value.type != VAL_INT) cast_error(vm, instr.line, target, value);
                        break;
                    case VAL_FLOAT:
                        if (value.type == VAL_INT) value = value_float((double)value.as.integer);
                        else if (value.type == VAL_FLOAT32) value = value_float(value.as.number);
                        else if (value.type != VAL_FLOAT) cast_error(vm, instr.line, target, value);
                        break;
                    case VAL_FLOAT32:
                        if (value.type == VAL_INT) value = value_float32((double)value.as.integer);
                        else if (value.type == VAL_FLOAT) value = value_float32(value.as.number);
                        else if (value.type != VAL_FLOAT32) cast_error(vm, instr.line, target, value);
                        break;
                    case VAL_STRING:
                        if (value.type != VAL_STRING) cast_error(vm, instr.line, target, value);
                        break;
                    case VAL_BOOL:
                        if (value.type != VAL_BOOL) cast_error(vm, instr.line, target, value);
                        break;
                    case VAL_LIST:
                        if (value.type != VAL_LIST) cast_error(vm, instr.line, target, value);
                        break;
                    case VAL_DICT:
                        if (value.type != VAL_DICT) cast_error(vm, instr.line, target, value);
                        break;
                    default:
                        break;
                }
                stack[sp - 1] = value;
                DISPATCH();
            }

            op_CONST:
                stack[sp++] = frame->closure->proto->constants[instr.c];
                DISPATCH();

            op_NIL:
                stack[sp++] = value_nil();
                DISPATCH();

            op_TRUE:
                stack[sp++] = value_bool(true);
                DISPATCH();

            op_FALSE:
                stack[sp++] = value_bool(false);
                DISPATCH();

            op_POP:
                sp--;
                DISPATCH();

            op_DUP:
                stack[sp] = stack[sp - 1];
                sp++;
                DISPATCH();

            op_GET_LOCAL:
                stack[sp++] = frame->slots[instr.c];
                DISPATCH();

            op_GET_LOCAL_CONST: {
                stack[sp++] = frame->slots[instr.a];
                stack[sp++] = frame->closure->proto->constants[instr.c];
                DISPATCH();
            }

            op_GET_GLOBAL_CONST: {
                stack[sp++] = vm->globals[instr.a];
                stack[sp++] = frame->closure->proto->constants[instr.c];
                DISPATCH();
            }

            op_GET_LOCAL_ADD: {
                Value local = frame->slots[instr.a];
                Value constant = frame->closure->proto->constants[instr.c];
                if (local.type == VAL_INT && constant.type == VAL_INT) {
                    stack[sp++] = value_int(local.as.integer + constant.as.integer);
                } else if (value_is_number(local) && value_is_number(constant)) {
                    stack[sp++] = make_number(promote(local.type, constant.type),
                                              to_double(local) + to_double(constant));
                } else if (local.type == VAL_STRING && constant.type == VAL_STRING) {
                    ObjString* result = string_concat(vm, (ObjString*)local.as.obj, (ObjString*)constant.as.obj);
                    stack[sp++] = value_obj(&result->obj);
                } else {
                    vm_runtime_error(vm, instr.line, "unsupported operand types for '+'");
                }
                DISPATCH();
            }

            op_ADD_CONST: {
                Value constant = frame->closure->proto->constants[instr.c];
                Value left = stack[--sp];
                if (left.type == VAL_INT && constant.type == VAL_INT) {
                    stack[sp++] = value_int(left.as.integer + constant.as.integer);
                } else if (value_is_number(left) && value_is_number(constant)) {
                    stack[sp++] = make_number(promote(left.type, constant.type),
                                              to_double(left) + to_double(constant));
                } else if (left.type == VAL_STRING && constant.type == VAL_STRING) {
                    ObjString* result = string_concat(vm, (ObjString*)left.as.obj, (ObjString*)constant.as.obj);
                    stack[sp++] = value_obj(&result->obj);
                } else {
                    vm_runtime_error(vm, instr.line, "unsupported operand types for '+'");
                }
                DISPATCH();
            }

            op_SET_LOCAL:
                frame->slots[instr.c] = copy_if_struct(vm, stack[sp - 1]);
                DISPATCH();

            op_GET_GLOBAL:
                stack[sp++] = vm->globals[instr.c];
                DISPATCH();

            op_SET_GLOBAL:
                vm->globals[instr.c] = copy_if_struct(vm, stack[sp - 1]);
                DISPATCH();

            op_GET_UPVAL: {
                ObjUpvalue* upvalue = frame->closure->upvalues[instr.c];
                stack[sp++] = upvalue->is_closed ? upvalue->closed : *upvalue->location;
                DISPATCH();
            }

            op_SET_UPVAL: {
                ObjUpvalue* upvalue = frame->closure->upvalues[instr.c];
                Value value = copy_if_struct(vm, stack[sp - 1]);
                if (upvalue->is_closed) upvalue->closed = value;
                else *upvalue->location = value;
                DISPATCH();
            }

            op_NATIVE:
                stack[sp++] = native_value(vm->natives[instr.c].entry);
                DISPATCH();

            op_ADD: {
                Value b = stack[--sp];
                Value a = stack[--sp];
                if (a.type == VAL_INT && b.type == VAL_INT) {
                    stack[sp++] = value_int(a.as.integer + b.as.integer);
                } else if (value_is_number(a) && value_is_number(b)) {
                    stack[sp++] = make_number(promote(a.type, b.type), to_double(a) + to_double(b));
                } else if (a.type == VAL_STRING && b.type == VAL_STRING) {
                    ObjString* result = string_concat(vm, (ObjString*)a.as.obj, (ObjString*)b.as.obj);
                    stack[sp++] = value_obj(&result->obj);
                } else if (a.type == VAL_LIST && b.type == VAL_LIST) {
                    ObjList* left = (ObjList*)a.as.obj;
                    ObjList* right = (ObjList*)b.as.obj;
                    ObjList* result = list_new(vm);
                    for (uint32_t i = 0; i < left->count; i++) list_push(vm, result, left->items[i]);
                    for (uint32_t i = 0; i < right->count; i++) list_push(vm, result, right->items[i]);
                    stack[sp++] = value_obj(&result->obj);
                } else {
                    vm_runtime_error(vm, instr.line, "unsupported operand types for '+'");
                }
                DISPATCH();
            }

            op_SUB: {
                Value b = stack[--sp];
                Value a = stack[--sp];
                if (a.type == VAL_INT && b.type == VAL_INT) {
                    stack[sp++] = value_int(a.as.integer - b.as.integer);
                } else if (value_is_number(a) && value_is_number(b)) {
                    stack[sp++] = make_number(promote(a.type, b.type), to_double(a) - to_double(b));
                } else {
                    vm_runtime_error(vm, instr.line, "unsupported operand types for '-'");
                }
                DISPATCH();
            }

            op_MUL: {
                Value b = stack[--sp];
                Value a = stack[--sp];
                if (a.type == VAL_INT && b.type == VAL_INT) {
                    stack[sp++] = value_int(a.as.integer * b.as.integer);
                } else if (value_is_number(a) && value_is_number(b)) {
                    stack[sp++] = make_number(promote(a.type, b.type), to_double(a) * to_double(b));
                } else if (a.type == VAL_STRING && b.type == VAL_INT) {
                    stack[sp++] = string_repeat(vm, (ObjString*)a.as.obj, b.as.integer, instr.line);
                } else if (a.type == VAL_INT && b.type == VAL_STRING) {
                    stack[sp++] = string_repeat(vm, (ObjString*)b.as.obj, a.as.integer, instr.line);
                } else if (a.type == VAL_LIST && b.type == VAL_INT) {
                    ObjList* source = (ObjList*)a.as.obj;
                    ObjList* result = list_new(vm);
                    for (int64_t i = 0; i < b.as.integer; i++) {
                        for (uint32_t j = 0; j < source->count; j++) list_push(vm, result, source->items[j]);
                    }
                    stack[sp++] = value_obj(&result->obj);
                } else {
                    vm_runtime_error(vm, instr.line, "unsupported operand types for '*'");
                }
                DISPATCH();
            }

            op_DIV: {
                Value b = stack[--sp];
                Value a = stack[--sp];
                if (!value_is_number(a) || !value_is_number(b)) {
                    vm_runtime_error(vm, instr.line, "unsupported operand types for '/'");
                }
                double divisor = to_double(b);
                if (divisor == 0.0) vm_runtime_error(vm, instr.line, "division by zero");
                ValueType type = promote(a.type, b.type);
                if (type == VAL_INT) type = VAL_FLOAT;
                stack[sp++] = make_number(type, to_double(a) / divisor);
                DISPATCH();
            }

op_MOD: {
                Value b = stack[--sp];
                Value a = stack[--sp];
                if (a.type != VAL_INT || b.type != VAL_INT) {
                    vm_runtime_error(vm, instr.line, "unsupported operand types for '%'");
                }
                if (b.as.integer == 0) vm_runtime_error(vm, instr.line, "division by zero");
                int64_t remainder = a.as.integer % b.as.integer;
                if (remainder != 0 && ((remainder < 0) != (b.as.integer < 0))) remainder += b.as.integer;
                stack[sp++] = value_int(remainder);
                DISPATCH();
            }

            op_NEG: {
                Value a = stack[sp - 1];
                if (a.type == VAL_INT) stack[sp - 1] = value_int(-a.as.integer);
                else if (a.type == VAL_FLOAT) stack[sp - 1] = value_float(-a.as.number);
                else if (a.type == VAL_FLOAT32) stack[sp - 1] = value_float32(-a.as.number);
                else vm_runtime_error(vm, instr.line, "unsupported operand type for unary '-'");
                DISPATCH();
            }

            op_NOT:
                stack[sp - 1] = value_bool(!value_is_truthy(stack[sp - 1]));
                DISPATCH();

            op_IN: {
                Value container = stack[sp - 1];
                Value needle = stack[sp - 2];
                bool found = false;

                if (container.type == VAL_LIST) {
                    ObjList* list = (ObjList*)container.as.obj;
                    for (uint32_t i = 0; i < list->count; i++) {
                        if (values_equal(list->items[i], needle)) { found = true; break; }
                    }
                } else if (container.type == VAL_DICT) {
                    ObjDict* dict = (ObjDict*)container.as.obj;
                    found = dict_get(dict, needle, NULL);
                } else if (container.type == VAL_STRING) {
                    ObjString* text = (ObjString*)container.as.obj;
                    ObjString* part = (ObjString*)needle.as.obj;
                    if (part->len == 0) found = true;
                    else if (part->len <= text->len) {
                        for (uint32_t i = 0; i + part->len <= text->len; i++) {
                            if (memcmp(text->chars + i, part->chars, part->len) == 0) {
                                found = true;
                                break;
                            }
                        }
                    }
                } else {
                    vm_runtime_error(vm, instr.line, "the value cannot be searched");
                }

                sp--;
                stack[sp - 1] = value_bool(found);
                DISPATCH();
            }

            op_EQ: {
                Value b = stack[--sp];
                Value a = stack[--sp];
                stack[sp++] = value_bool(values_equal(a, b));
                DISPATCH();
            }

            op_NE: {
                Value b = stack[--sp];
                Value a = stack[--sp];
                stack[sp++] = value_bool(!values_equal(a, b));
                DISPATCH();
            }

            op_LT:
            op_LE:
            op_GT:
            op_GE: {
                Value b = stack[--sp];
                Value a = stack[--sp];
                int result = 0;
                if (!values_compare(a, b, &result)) {
                    vm_runtime_error(vm, instr.line, "values are not comparable");
                }
                bool value = false;
                switch (instr.op) {
                    case OP_LT: value = result < 0; break;
                    case OP_LE: value = result <= 0; break;
                    case OP_GT: value = result > 0; break;
                    default:   value = result >= 0; break;
                }
                stack[sp++] = value_bool(value);
                DISPATCH();
            }

            op_BITAND:
            op_BITOR:
            op_BITXOR:
            op_SHL:
            op_SHR: {
                Value b = stack[--sp];
                Value a = stack[--sp];
                if (a.type != VAL_INT || b.type != VAL_INT) {
                    vm_runtime_error(vm, instr.line, "bitwise operators require integers");
                }
                int64_t result = 0;
                switch (instr.op) {
                    case OP_BITAND: result = a.as.integer & b.as.integer; break;
                    case OP_BITOR:  result = a.as.integer | b.as.integer; break;
                    case OP_BITXOR: result = a.as.integer ^ b.as.integer; break;
                    case OP_SHL:    result = a.as.integer << b.as.integer; break;
                    default:        result = a.as.integer >> b.as.integer; break;
                }
                stack[sp++] = value_int(result);
                DISPATCH();
            }

            op_JMP:
                ip = frame->closure->proto->code + instr.c;
                DISPATCH();

            op_JMP_IF_FALSE: {
                Value value = stack[--sp];
                if (!value_is_truthy(value)) {
                    ip = frame->closure->proto->code + instr.c;
                }
                DISPATCH();
            }

            op_JMP_IF_TRUE: {
                Value value = stack[--sp];
                if (value_is_truthy(value)) {
                    ip = frame->closure->proto->code + instr.c;
                }
                DISPATCH();
            }

            op_CALL: {
                int argc = instr.a;
                int base = sp - argc - 1;
Value callee = stack[base];

                if (callee.type == VAL_CLOSURE) {
                    ObjClosure* target = (ObjClosure*)callee.as.obj;
                    if (target->proto->arity != argc) {
                        vm_runtime_error(vm, instr.line, "function '%s' expects %d argument(s), got %d",
                                         target->proto->name, target->proto->arity, argc);
                    }
Value* args = &stack[base + 1];
                    sp = base;
                    frame->ip = ip; // remember the return address
                    vm_enter(vm, target, args, argc, instr.line, base);
                    frame = &vm->frames[vm->frame_count - 1];
                    ip = frame->ip;
                } else if (callee.type == VAL_NATIVE) {
                    Value* args = &stack[base + 1];
                    vm->stack_top = sp;
                    Value result = callee.as.native->fn(vm, argc, args, callee.as.native->user);
                    sp = vm->stack_top;
                    sp = base;
                    stack[sp++] = result;
                    frame->ip = ip; // keep the trace pointing at the call site
                } else if (callee.type == VAL_HOSTCLASS) {
                    // a constructor of a class of the host
                    HostClass* klass = (HostClass*)callee.as.pointer;
                    if (!klass->constructor) {
                        vm_runtime_error(vm, instr.line, "class '%s' has no constructor",
                                         klass->name);
                    }
                    Value* args = &stack[base + 1];
                    Value result = klass->constructor(vm, argc, args, klass->constructor_user);
                    vm->stack_top = base;
                    stack[sp++] = result;
                } else if (callee.type == VAL_CLASS) {
                    // a constructor call
                    ObjClass* klass = (ObjClass*)callee.as.obj;
                    ObjInstance* instance = instance_new(vm, klass);
                    stack[base] = value_obj(&instance->obj);

                    ObjClosure* init = klass->init;
                    int index = vm_find_method(vm, klass, "init");
                    if (index >= 0) init = klass->methods[index];

                    if (init) {
                        if (init->proto->arity != argc) {
                            vm_runtime_error(vm, instr.line,
                                             "'%s' expects %d argument(s), got %d",
                                             klass->name, init->proto->arity, argc);
                        }
                        Value* args = &stack[base]; // args[0] is the new instance
                        // the instance stays on the stack, the temporaries of
                        // "init" are placed above it
                        sp = base + 1;
                        frame->ip = ip;
                        // the instance stays on the stack, the result of "init" is dropped
                        vm_enter(vm, init, args, argc + 1, instr.line, base + 1);
                        vm->frames[vm->frame_count - 1].discards_result = true;
                        frame = &vm->frames[vm->frame_count - 1];
                        ip = frame->ip;
                    } else {
                        sp = base + 1; // keep the instance on the stack
                    }
                } else {
                    vm_runtime_error(vm, instr.line, "attempt to call a non-function value");
                }
                DISPATCH();
            }

op_METHOD: {
                int argc = instr.a;
                int base = sp - argc - 1;
                Value receiver = stack[base];

                if (receiver.type == VAL_USERDATA) {
                    // a method of a class of the host
                    ObjUserdata* object = (ObjUserdata*)receiver.as.obj;
                    const char* name = method_group_name(instr.c);
                    int index = host_class_find_method(object->klass, name);
                    if (index < 0) {
                        vm_runtime_error(vm, instr.line, "class '%s' has no method '%s'",
                                         object->klass->name, name);
                    }
                    HostMethod* method = &object->klass->methods[index];
                    Value* args = &stack[base]; // args[0] is the receiver
                    Value result = method->fn(vm, argc + 1, args, method->user);
                    sp = base;
                    stack[sp++] = result;
                    DISPATCH();
                }

                if (receiver.type == VAL_OBJECT) {
                    // a method of a user defined class
                    ObjInstance* instance = (ObjInstance*)receiver.as.obj;
                    const char* name = method_group_name(instr.c);
                    int index = vm_find_method(vm, instance->klass, name);
                    if (index < 0) {
                        vm_runtime_error(vm, instr.line, "class '%s' has no method '%s'",
                                         instance->klass->name, name);
                    }
                    ObjClosure* method = instance->klass->methods[index];
                    if (method->proto->arity != argc) {
                        vm_runtime_error(vm, instr.line, "method '%s' expects %d argument(s), got %d",
                                         name, method->proto->arity, argc);
                    }
                    Value* args = &stack[base]; // args[0] is the receiver (self)
                    sp = base;
                    frame->ip = ip;
                    vm_enter(vm, method, args, argc + 1, instr.line, base);
                    frame = &vm->frames[vm->frame_count - 1];
                    ip = frame->ip;
                    DISPATCH();
                }

                NativeFn method = method_lookup(instr.c, receiver.type);
                if (!method) {
                    vm_runtime_error(vm, instr.line, "type has no method '%s'",
                                     method_group_name(instr.c));
                }
                Value* args = &stack[base]; // args[0] is the receiver
                vm->stack_top = sp;
                Value result = method(vm, argc + 1, args, NULL);
                sp = vm->stack_top;
                sp = base;
                stack[sp++] = result;
                DISPATCH();
            }

            op_METHOD_NAME: {
                int argc = instr.a;
                int base = sp - argc - 1;
                Value receiver = stack[base];
                ObjString* name = (ObjString*)frame->closure->proto->constants[instr.c].as.obj;

                if (receiver.type == VAL_USERDATA) {
                    // a method of a class of the host
                    ObjUserdata* object = (ObjUserdata*)receiver.as.obj;
                    int index = host_class_find_method(object->klass, name->chars);
                    if (index < 0) {
                        vm_runtime_error(vm, instr.line, "class '%s' has no method '%s'",
                                         object->klass->name, name->chars);
                    }
                    HostMethod* method = &object->klass->methods[index];
                    Value* args = &stack[base];
                    Value result = method->fn(vm, argc + 1, args, method->user);
                    sp = base;
                    stack[sp++] = result;
                    DISPATCH();
                }

                if (receiver.type != VAL_OBJECT) {
                    vm_runtime_error(vm, instr.line, "type has no method '%s'", name->chars);
                }
                ObjInstance* instance = (ObjInstance*)receiver.as.obj;
                int index = vm_find_method(vm, instance->klass, name->chars);
                if (index < 0) {
                    vm_runtime_error(vm, instr.line, "class '%s' has no method '%s'",
                                     instance->klass->name, name->chars);
                }
                ObjClosure* method = instance->klass->methods[index];
                if (method->proto->arity != argc) {
                    vm_runtime_error(vm, instr.line, "method '%s' expects %d argument(s), got %d",
                                     name->chars, method->proto->arity, argc);
                }
                Value* args = &stack[base]; // args[0] is the receiver (self)
                sp = base;
                frame->ip = ip;
                vm_enter(vm, method, args, argc + 1, instr.line, base);
                frame = &vm->frames[vm->frame_count - 1];
                ip = frame->ip;
                DISPATCH();
            }

            op_ITER_INIT: {
                Value target = stack[--sp];
                ObjIterator* iter = iterator_new(vm, target);
                stack[sp++] = value_obj(&iter->obj);
                DISPATCH();
            }

            op_ITER_NEXT: {
                ObjIterator* iter = (ObjIterator*)stack[sp - 1].as.obj;
                uint32_t limit = iter->count;
                if (iter->kind == ITER_LIST) {
                    uint32_t current = ((ObjList*)iter->target.as.obj)->count;
                    if (current < limit) limit = current;
                }
                if (iter->index >= limit) {
                    ip = frame->closure->proto->code + instr.c;
                    DISPATCH();
                }
                Value next;
                switch (iter->kind) {
                    case ITER_LIST:
                        next = ((ObjList*)iter->target.as.obj)->items[iter->index];
                        break;
                    case ITER_STRING: {
                        ObjString* str = (ObjString*)iter->target.as.obj;
                        ObjString* ch = string_new(vm, str->chars + iter->index, 1);
                        next = value_obj(&ch->obj);
                        break;
                    }
                    default:
                        next = iter->keys->items[iter->index];
                        break;
                }
                iter->index++;
                stack[sp++] = next;
                DISPATCH();
            }

            op_RETURN: {
                Value result = stack[--sp];
                CallFrame* current = &vm->frames[vm->frame_count - 1];
                close_upvalues_from(vm, current->slots);
                vm->arena_top = current->slot_base; // release the frame arena
                vm->frame_count--;
                vm->handler_count = current->handler_base;
                // restore the caller stack: the result takes the callee slot
                sp = current->stack_base;
                if (!current->discards_result) {
                    stack[sp++] = result;
                }
                if (vm->frame_count <= stop_frame) {
                    vm->error_jump = saved_jump;
                    return true;
                }
                frame = &vm->frames[vm->frame_count - 1];
                ip = frame->ip;
                DISPATCH();
            }

            op_CLOSURE: {
                FunctionProto* proto = vm->program->protos[instr.c];
                ObjClosure* created = closure_new(vm, proto);
                for (int i = 0; i < proto->upvalue_count; i++) {
                    UpvalueDesc* desc = &proto->upvalues[i];
                    if (desc->is_local) {
                        created->upvalues[i] = capture_upvalue(vm, frame->slots + desc->index);
                    } else {
                        created->upvalues[i] = frame->closure->upvalues[desc->index];
                    }
                }
                stack[sp++] = value_obj(&created->obj);
                DISPATCH();
            }

            op_CLOSE_UPVAL:
                close_upvalues_from(vm, frame->slots + instr.c);
                DISPATCH();

            op_LIST: {
                int count = instr.a;
                ObjList* list = list_new(vm);
                for (int i = 0; i < count; i++) {
                    list_push(vm, list, stack[sp - count + i]);
                }
                sp -= count;
                stack[sp++] = value_obj(&list->obj);
                DISPATCH();
            }

            op_DICT: {
                int pairs = instr.a;
                ObjDict* dict = dict_new(vm);
                for (int i = 0; i < pairs; i++) {
                    Value value = stack[sp - 1];
                    Value key = stack[sp - 2];
                    dict_set(vm, dict, key, value);
                    sp -= 2;
                }
                stack[sp++] = value_obj(&dict->obj);
                DISPATCH();
            }

            op_GET_INDEX: {
                Value index = stack[--sp];
                Value container = stack[--sp];
                stack[sp++] = get_index(vm, container, index, instr.line);
                DISPATCH();
            }

            op_GET_INDEX_KEEP: {
                Value index = stack[sp - 1];
                Value container = stack[sp - 2];
                stack[sp++] = get_index(vm, container, index, instr.line);
                DISPATCH();
            }

            op_SET_INDEX: {
                // [container, index, value] -> [value]
                Value value = stack[sp - 1];
                Value index = stack[sp - 2];
                Value container = stack[sp - 3];
                set_index(vm, container, index, value, instr.line);
                sp -= 2;
                DISPATCH();
            }

op_LEN: {
                Value container = stack[--sp];
                if (container.type == VAL_STRING) {
                    stack[sp++] = value_int(((ObjString*)container.as.obj)->len);
                } else if (container.type == VAL_LIST) {
                    stack[sp++] = value_int(((ObjList*)container.as.obj)->count);
                } else if (container.type == VAL_DICT) {
                    stack[sp++] = value_int(((ObjDict*)container.as.obj)->count);
                } else {
                    vm_runtime_error(vm, instr.line, "value has no length");
                }
                DISPATCH();
            }

            op_default:
                vm_runtime_error(vm, instr.line, "unknown instruction");
                DISPATCH();
}
