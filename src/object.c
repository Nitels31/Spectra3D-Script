// Spectra3D Script - objects, strings and lists.
//
// Every heap object lives on the vm->objects list. There is no garbage
// collector, so the runtime never pauses; all objects are released at once
// when the virtual machine is destroyed => no memory leaks.
#include "internal/vm.h"
#include "internal/dict.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------
// strings
// ------------------------------------------------------------------

uint32_t string_hash(const char* chars, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= (unsigned char)chars[i];
        hash *= 16777619u;
    }
    return hash;
}

ObjString* string_new(VM* vm, const char* chars, size_t len) {
    ObjString* str = (ObjString*)malloc(sizeof(ObjString) + len + 1);
    if (!str) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    str->obj.type = OBJ_STRING;
    str->obj.next = vm->objects;
    vm->objects = &str->obj;
    str->len = (uint32_t)len;
    str->hash = string_hash(chars, len);
    if (len) memcpy(str->chars, chars, len);
    str->chars[len] = '\0';
    mem_account(vm, sizeof(ObjString) + len + 1);
    return str;
}

ObjString* string_new_cstr(VM* vm, const char* cstr) {
    return string_new(vm, cstr, strlen(cstr));
}

ObjString* string_concat(VM* vm, ObjString* a, ObjString* b) {
    size_t len = (size_t)a->len + (size_t)b->len;
    ObjString* str = (ObjString*)malloc(sizeof(ObjString) + len + 1);
    if (!str) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    str->obj.type = OBJ_STRING;
    str->obj.next = vm->objects;
    vm->objects = &str->obj;
    str->len = (uint32_t)len;
    memcpy(str->chars, a->chars, a->len);
    memcpy(str->chars + a->len, b->chars, b->len);
    str->chars[len] = '\0';
    str->hash = string_hash(str->chars, len);
    mem_account(vm, sizeof(ObjString) + len + 1);
    return str;
}

// ------------------------------------------------------------------
// lists
// ------------------------------------------------------------------

ObjList* list_new(VM* vm) {
    ObjList* list = (ObjList*)malloc(sizeof(ObjList));
    if (!list) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    list->obj.type = OBJ_LIST;
    list->obj.next = vm->objects;
    vm->objects = &list->obj;
    list->count = 0;
    list->capacity = 0;
    list->items = NULL;
    mem_account(vm, sizeof(ObjList));
    return list;
}

void list_push(VM* vm, ObjList* list, Value value) {
    if (list->count == list->capacity) {
        uint32_t old_capacity = list->capacity;
        uint32_t new_capacity = list->capacity ? list->capacity * 2 : 8;
        Value* items = (Value*)realloc(list->items, (size_t)new_capacity * sizeof(Value));
        if (!items) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        list->items = items;
        list->capacity = new_capacity;
        mem_bytes(vm, (size_t)(new_capacity - old_capacity) * sizeof(Value));
    }
    list->items[list->count++] = value;
}

// ------------------------------------------------------------------
// closures
// ------------------------------------------------------------------

ObjClosure* closure_new(VM* vm, FunctionProto* proto) {
    size_t size = sizeof(ObjClosure) + sizeof(ObjUpvalue*) * (size_t)proto->upvalue_count;
    ObjClosure* closure = (ObjClosure*)malloc(size);
    if (!closure) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    closure->obj.type = OBJ_CLOSURE;
    closure->obj.next = vm->objects;
    vm->objects = &closure->obj;
    closure->proto = proto;
    closure->upvalue_count = (uint8_t)proto->upvalue_count;
    for (int i = 0; i < proto->upvalue_count; i++) closure->upvalues[i] = NULL;
    mem_account(vm, size);
    return closure;
}

// ------------------------------------------------------------------
// memory
// ------------------------------------------------------------------

static void free_object(Obj* obj) {
    if (obj->type == OBJ_LIST) {
        ObjList* list = (ObjList*)obj;
        free(list->items);
        list->items = NULL;
    } else if (obj->type == OBJ_DICT) {
        ObjDict* dict = (ObjDict*)obj;
        free(dict->entries);
        dict->entries = NULL;
    } else if (obj->type == OBJ_CLASS) {
        ObjClass* klass = (ObjClass*)obj;
        for (uint32_t i = 0; i < klass->field_count; i++) free(klass->field_names[i]);
        for (uint32_t i = 0; i < klass->method_count; i++) free(klass->method_names[i]);
        free(klass->field_names);
        free(klass->method_names);
        free(klass->methods);
        free(klass->name);
    } else if (obj->type == OBJ_INSTANCE) {
        ObjInstance* instance = (ObjInstance*)obj;
        free(instance->fields);
        instance->fields = NULL;
    } else if (obj->type == OBJ_USERDATA) {
        ObjUserdata* object = (ObjUserdata*)obj;
        if (object->klass && object->klass->free_fn) {
            object->klass->free_fn(object->data);
        }
    }
    free(obj);
}

void mem_account(VM* vm, size_t bytes) {
    vm->bytes_allocated += bytes;
    vm->object_count++;
}

void mem_bytes(VM* vm, size_t bytes) {
    vm->bytes_allocated += bytes;
}

void objects_free_all(VM* vm) {
    Obj* obj = vm->objects;
    while (obj) {
        Obj* next = obj->next;
        free_object(obj);
        obj = next;
    }
    vm->objects = NULL;
    vm->object_count = 0;
}

// ------------------------------------------------------------------
// the host objects
// ------------------------------------------------------------------

HostClass* host_class_new(const char* name) {
    HostClass* klass = (HostClass*)calloc(1, sizeof(HostClass));
    if (!klass) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    klass->name = strdup(name ? name : "?");
    return klass;
}

int host_class_add_method(HostClass* klass, const char* name, NativeFn fn, void* user) {
    for (int i = 0; i < klass->method_count; i++) {
        if (strcmp(klass->methods[i].name, name) == 0) {
            klass->methods[i].fn = fn;
            klass->methods[i].user = user;
            return i;
        }
    }
    if (klass->method_count == klass->method_cap) {
        int capacity = klass->method_cap ? klass->method_cap * 2 : 8;
        klass->methods = (HostMethod*)realloc(klass->methods, sizeof(HostMethod) * (size_t)capacity);
        if (!klass->methods) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        klass->method_cap = capacity;
    }
    HostMethod* method = &klass->methods[klass->method_count];
    method->name = strdup(name);
    method->fn = fn;
    method->user = user;
    return klass->method_count++;
}

int host_class_find_method(HostClass* klass, const char* name) {
    for (int i = 0; i < klass->method_count; i++) {
        if (strcmp(klass->methods[i].name, name) == 0) return i;
    }
    return -1;
}

int host_class_add_property(HostClass* klass, const char* name, NativeFn getter,
                            NativeFn setter, void* user) {
    for (int i = 0; i < klass->property_count; i++) {
        if (strcmp(klass->properties[i].name, name) == 0) {
            klass->properties[i].getter = getter;
            klass->properties[i].setter = setter;
            klass->properties[i].user = user;
            return i;
        }
    }
    if (klass->property_count == klass->property_cap) {
        int capacity = klass->property_cap ? klass->property_cap * 2 : 8;
        klass->properties = (HostProperty*)realloc(klass->properties,
                                                   sizeof(HostProperty) * (size_t)capacity);
        if (!klass->properties) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        klass->property_cap = capacity;
    }
    HostProperty* property = &klass->properties[klass->property_count];
    property->name = strdup(name);
    property->getter = getter;
    property->setter = setter;
    property->user = user;
    return klass->property_count++;
}

int host_class_find_property(HostClass* klass, const char* name) {
    for (int i = 0; i < klass->property_count; i++) {
        if (strcmp(klass->properties[i].name, name) == 0) return i;
    }
    return -1;
}

ObjUserdata* userdata_new(VM* vm, HostClass* klass, void* data) {
    ObjUserdata* object = (ObjUserdata*)malloc(sizeof(ObjUserdata));
    if (!object) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    object->obj.type = OBJ_USERDATA;
    object->obj.next = vm->objects;
    vm->objects = &object->obj;
    object->klass = klass;
    object->data = data;
    mem_account(vm, sizeof(ObjUserdata));
    return object;
}

// ------------------------------------------------------------------
// classes and instances
// ------------------------------------------------------------------

ObjClass* class_new(VM* vm, const char* name, bool is_struct) {
    ObjClass* klass = (ObjClass*)malloc(sizeof(ObjClass));
    if (!klass) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    klass->obj.type = OBJ_CLASS;
    klass->obj.next = vm->objects;
    vm->objects = &klass->obj;
    klass->name = strdup(name ? name : "?");
    klass->is_struct = is_struct;
    klass->field_count = 0;
    klass->field_names = NULL;
    klass->method_count = 0;
    klass->method_names = NULL;
    klass->methods = NULL;
    klass->init = NULL;
    mem_account(vm, sizeof(ObjClass));
    return klass;
}

ObjInstance* instance_new(VM* vm, ObjClass* klass) {
    ObjInstance* instance = (ObjInstance*)malloc(sizeof(ObjInstance));
    if (!instance) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    instance->obj.type = OBJ_INSTANCE;
    instance->obj.next = vm->objects;
    vm->objects = &instance->obj;
    instance->klass = klass;
    instance->field_count = klass->field_count;
    instance->fields = (Value*)malloc(sizeof(Value) * (instance->field_count + 1));
    for (uint32_t i = 0; i < instance->field_count; i++) {
        instance->fields[i] = value_nil();
    }
    mem_account(vm, sizeof(ObjInstance) + sizeof(Value) * instance->field_count);
    return instance;
}

int class_find_field(ObjClass* klass, const char* name) {
    for (uint32_t i = 0; i < klass->field_count; i++) {
        if (strcmp(klass->field_names[i], name) == 0) return (int)i;
    }
    return -1;
}

int class_find_method(ObjClass* klass, const char* name) {
    for (uint32_t i = 0; i < klass->method_count; i++) {
        if (strcmp(klass->method_names[i], name) == 0) return (int)i;
    }
    return -1;
}

ObjInstance* instance_copy(VM* vm, ObjInstance* source) {
    ObjInstance* copy = instance_new(vm, source->klass);
    for (uint32_t i = 0; i < source->field_count; i++) {
        copy->fields[i] = source->fields[i];
    }
    return copy;
}

// ------------------------------------------------------------------
// iterators
// ------------------------------------------------------------------

ObjIterator* iterator_new(VM* vm, Value target) {
    ObjIterator* iter = (ObjIterator*)malloc(sizeof(ObjIterator));
    if (!iter) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    iter->obj.type = OBJ_ITERATOR;
    iter->obj.next = vm->objects;
    vm->objects = &iter->obj;
    iter->index = 0;
    iter->keys = NULL;
    iter->target = target;

    switch (target.type) {
        case VAL_LIST:
            iter->kind = ITER_LIST;
            iter->count = ((ObjList*)target.as.obj)->count;
            break;
        case VAL_STRING:
            iter->kind = ITER_STRING;
            iter->count = ((ObjString*)target.as.obj)->len;
            break;
        case VAL_DICT: {
            iter->kind = ITER_DICT;
            ObjDict* dict = (ObjDict*)target.as.obj;
            ObjList* keys = list_new(vm);
            for (uint32_t i = 0; i < dict->capacity; i++) {
                if (dict->entries[i].state == 1) {
                    list_push(vm, keys, dict->entries[i].key);
                }
            }
            iter->keys = keys;
            iter->count = keys->count;
            break;
        }
        default:
            free(iter);
            vm_runtime_error(vm, 0, "value is not iterable");
            return NULL;
    }
    mem_account(vm, sizeof(ObjIterator));
    return iter;
}

const char* value_type_name(ValueType type) {
    switch (type) {
        case VAL_NIL:     return "nil";
        case VAL_BOOL:    return "bool";
        case VAL_INT:     return "int";
        case VAL_FLOAT:   return "double";
        case VAL_FLOAT32: return "float";
        case VAL_STRING:  return "str";
        case VAL_LIST:    return "list";
        case VAL_DICT:    return "dict";
        case VAL_USERDATA: return "object";
        case VAL_HOSTCLASS: return "class";
        case VAL_OBJECT:  return "object";
        case VAL_CLASS:   return "class";
        case VAL_CLOSURE: return "fn";
        case VAL_NATIVE:  return "fn";
    }
    return "?";
}

bool type_from_name(const char* name, int length, ValueType* out) {
    static const struct {
        const char* name;
        ValueType   type;
    } TYPES[] = {
        { "int", VAL_INT },
        { "float", VAL_FLOAT32 },
        { "double", VAL_FLOAT },
        { "str", VAL_STRING },
        { "string", VAL_STRING },
        { "bool", VAL_BOOL },
        { "list", VAL_LIST },
        { "dict", VAL_DICT }
    };
    for (size_t i = 0; i < sizeof(TYPES) / sizeof(TYPES[0]); i++) {
        if ((int)strlen(TYPES[i].name) == length &&
            memcmp(TYPES[i].name, name, (size_t)length) == 0) {
            *out = TYPES[i].type;
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------
// comparison
// ------------------------------------------------------------------
bool values_equal(Value a, Value b) {
    if (value_is_number(a) && value_is_number(b)) {
        if (a.type == VAL_INT && b.type == VAL_INT) return a.as.integer == b.as.integer;
        double x = a.type == VAL_INT ? (double)a.as.integer : a.as.number;
        double y = b.type == VAL_INT ? (double)b.as.integer : b.as.number;
        return x == y;
    }
    if (a.type == VAL_STRING && b.type == VAL_STRING) {
        ObjString* x = (ObjString*)a.as.obj;
        ObjString* y = (ObjString*)b.as.obj;
        return x->len == y->len && memcmp(x->chars, y->chars, x->len) == 0;
    }
    if (a.type == VAL_LIST && b.type == VAL_LIST) {
        ObjList* x = (ObjList*)a.as.obj;
        ObjList* y = (ObjList*)b.as.obj;
        if (x->count != y->count) return false;
        for (uint32_t i = 0; i < x->count; i++) {
            if (!values_equal(x->items[i], y->items[i])) return false;
        }
        return true;
    }
    if (a.type == VAL_BOOL && b.type == VAL_BOOL) return a.as.boolean == b.as.boolean;
    if (a.type == VAL_NIL && b.type == VAL_NIL) return true;
    if (a.type == VAL_NATIVE && b.type == VAL_NATIVE) return a.as.native == b.as.native;
    if (a.type == VAL_CLOSURE && b.type == VAL_CLOSURE) return a.as.obj == b.as.obj;
    return false;
}

bool values_compare(Value a, Value b, int* result) {
    if (value_is_number(a) && value_is_number(b)) {
        if (a.type == VAL_INT && b.type == VAL_INT) {
            *result = a.as.integer < b.as.integer ? -1 : (a.as.integer > b.as.integer ? 1 : 0);
        } else {
            double x = a.type == VAL_INT ? (double)a.as.integer : a.as.number;
            double y = b.type == VAL_INT ? (double)b.as.integer : b.as.number;
            *result = x < y ? -1 : (x > y ? 1 : 0);
        }
        return true;
    }
    if (a.type == VAL_STRING && b.type == VAL_STRING) {
        ObjString* x = (ObjString*)a.as.obj;
        ObjString* y = (ObjString*)b.as.obj;
        size_t min_len = x->len < y->len ? x->len : y->len;
        int cmp = min_len ? memcmp(x->chars, y->chars, min_len) : 0;
        if (cmp == 0) cmp = x->len < y->len ? -1 : (x->len > y->len ? 1 : 0);
        *result = cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
        return true;
    }
    return false;
}

// ------------------------------------------------------------------
// output
// ------------------------------------------------------------------

// the shortest representation that reads back to the same value
static void format_double(char* buffer, size_t size, double value) {
    for (int precision = 15; precision <= 17; precision++) {
        snprintf(buffer, size, "%.*g", precision, value);
        if (strtod(buffer, NULL) == value) break;
    }
}

static void write_double(FILE* out, double value) {
    char buffer[64];
    format_double(buffer, sizeof(buffer), value);
    // if the result looks like an integer - add ".0" like Python does
    bool has_dot = false;
    for (const char* p = buffer; *p; p++) {
        if (*p == '.' || *p == 'e' || *p == 'E' || *p == 'n' || *p == 'i') {
            has_dot = true;
            break;
        }
    }
    fputs(buffer, out);
    if (!has_dot) fputs(".0", out);
}

static void write_float32(FILE* out, double value) {
    char buffer[64];
    float single = (float)value;
    for (int precision = 6; precision <= 9; precision++) {
        snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
        if ((float)strtod(buffer, NULL) == single) break;
    }
    bool has_dot = false;
    for (const char* p = buffer; *p; p++) {
        if (*p == '.' || *p == 'e' || *p == 'E' || *p == 'n' || *p == 'i') {
            has_dot = true;
            break;
        }
    }
    fputs(buffer, out);
    if (!has_dot) fputs(".0", out);
}

void value_write(FILE* out, Value value) {
    switch (value.type) {
        case VAL_NIL:
            fputs("nil", out);
            break;
        case VAL_BOOL:
            fputs(value.as.boolean ? "true" : "false", out);
            break;
        case VAL_INT:
            fprintf(out, "%lld", (long long)value.as.integer);
            break;
        case VAL_FLOAT:
            write_double(out, value.as.number);
            break;
        case VAL_FLOAT32:
            write_float32(out, value.as.number);
            break;
        case VAL_STRING: {
            ObjString* str = (ObjString*)value.as.obj;
            fwrite(str->chars, 1, str->len, out);
            break;
        }
        case VAL_LIST: {
            ObjList* list = (ObjList*)value.as.obj;
            fputc('[', out);
            for (uint32_t i = 0; i < list->count; i++) {
                if (i) fputs(", ", out);
                value_write(out, list->items[i]);
            }
            fputc(']', out);
            break;
        }
        case VAL_DICT: {
            ObjDict* dict = (ObjDict*)value.as.obj;
            fputc('{', out);
            bool first = true;
            for (uint32_t i = 0; i < dict->capacity; i++) {
                if (dict->entries[i].state != 1) continue;
                if (!first) fputs(", ", out);
                first = false;
                value_write(out, dict->entries[i].key);
                fputs(": ", out);
                value_write(out, dict->entries[i].value);
            }
            fputc('}', out);
            break;
        }
        case VAL_HOSTCLASS: {
            HostClass* klass = (HostClass*)value.as.pointer;
            fprintf(out, "<class %s>", klass->name);
            break;
        }
        case VAL_USERDATA: {
            ObjUserdata* object = (ObjUserdata*)value.as.obj;
            fprintf(out, "<%s object>", object->klass ? object->klass->name : "host");
            break;
        }
        case VAL_OBJECT: {
            ObjInstance* instance = (ObjInstance*)value.as.obj;
            fprintf(out, "%s{", instance->klass->name);
            for (uint32_t i = 0; i < instance->field_count; i++) {
                if (i) fputs(", ", out);
                fprintf(out, "%s: ", instance->klass->field_names[i]);
                value_write(out, instance->fields[i]);
            }
            fputc('}', out);
            break;
        }
        case VAL_CLASS: {
            ObjClass* klass = (ObjClass*)value.as.obj;
            fprintf(out, "<class %s>", klass->name);
            break;
        }
        case VAL_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)value.as.obj;
            fprintf(out, "<fn %s>", closure->proto->name ? closure->proto->name : "?");
            break;
        }
        case VAL_NATIVE:
            fputs("<native fn>", out);
            break;
    }
}

ObjString* string_from_value(VM* vm, Value value) {
    if (value.type == VAL_STRING) {
        ObjString* str = (ObjString*)value.as.obj;
        return string_new(vm, str->chars, str->len);
    }

    char buffer[128];
    size_t length = 0;
    switch (value.type) {
        case VAL_NIL:
            length = (size_t)snprintf(buffer, sizeof(buffer), "nil");
            break;
        case VAL_BOOL:
            length = (size_t)snprintf(buffer, sizeof(buffer), "%s", value.as.boolean ? "true" : "false");
            break;
        case VAL_INT:
            length = (size_t)snprintf(buffer, sizeof(buffer), "%lld", (long long)value.as.integer);
            break;
        case VAL_FLOAT: {
            char tmp[64];
            format_double(tmp, sizeof(tmp), value.as.number);
            bool has_dot = false;
            for (size_t i = 0; i < strlen(tmp); i++) {
                if (tmp[i] == '.' || tmp[i] == 'e' || tmp[i] == 'E') { has_dot = true; break; }
            }
            length = (size_t)snprintf(buffer, sizeof(buffer), "%s%s", tmp, has_dot ? "" : ".0");
            break;
        }
        case VAL_FLOAT32: {
            char tmp[64];
            size_t tmp_len = (size_t)snprintf(tmp, sizeof(tmp), "%.9g", value.as.number);
            bool has_dot = false;
            for (size_t i = 0; i < tmp_len; i++) {
                if (tmp[i] == '.' || tmp[i] == 'e' || tmp[i] == 'E') { has_dot = true; break; }
            }
            length = (size_t)snprintf(buffer, sizeof(buffer), "%s%s", tmp, has_dot ? "" : ".0");
            break;
        }
        case VAL_LIST: {
            ObjList* list = (ObjList*)value.as.obj;
            size_t total = 2;
            for (uint32_t i = 0; i < list->count; i++) {
                ObjString* item = string_from_value(vm, list->items[i]);
                total += item->len + 2;
            }
            char* buffer2 = (char*)malloc(total + 1);
            size_t pos = 0;
            buffer2[pos++] = '[';
            for (uint32_t i = 0; i < list->count; i++) {
                if (i) { buffer2[pos++] = ','; buffer2[pos++] = ' '; }
                ObjString* item = string_from_value(vm, list->items[i]);
                memcpy(buffer2 + pos, item->chars, item->len);
                pos += item->len;
            }
            buffer2[pos++] = ']';
            ObjString* result = string_new(vm, buffer2, pos);
            free(buffer2);
            return result;
        }
        case VAL_CLASS: {
            ObjClass* klass = (ObjClass*)value.as.obj;
            length = (size_t)snprintf(buffer, sizeof(buffer), "<class %s>", klass->name);
            break;
        }
        case VAL_HOSTCLASS: {
            HostClass* klass = (HostClass*)value.as.pointer;
            length = (size_t)snprintf(buffer, sizeof(buffer), "<class %s>", klass->name);
            break;
        }
        case VAL_USERDATA: {
            ObjUserdata* object = (ObjUserdata*)value.as.obj;
            length = (size_t)snprintf(buffer, sizeof(buffer), "<%s object>",
                                      object->klass ? object->klass->name : "host");
            break;
        }
        case VAL_OBJECT: {
            ObjInstance* instance = (ObjInstance*)value.as.obj;
            length = (size_t)snprintf(buffer, sizeof(buffer), "%s{", instance->klass->name);
            for (uint32_t i = 0; i < instance->field_count && length < sizeof(buffer) - 32; i++) {
                ObjString* item = string_from_value(vm, instance->fields[i]);
                length += (size_t)snprintf(buffer + length, sizeof(buffer) - length, "%s%s: ",
                                          i ? ", " : "", instance->klass->field_names[i]);
                length += (size_t)snprintf(buffer + length, sizeof(buffer) - length, "%s",
                                          item->chars);
            }
            length += (size_t)snprintf(buffer + length, sizeof(buffer) - length, "}");
            break;
        }
        case VAL_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)value.as.obj;
            length = (size_t)snprintf(buffer, sizeof(buffer), "<fn %s>",
                                      closure->proto->name ? closure->proto->name : "?");
            break;
        }
        case VAL_NATIVE:
            length = (size_t)snprintf(buffer, sizeof(buffer), "<native fn>");
            break;
        default:
            length = 0;
            break;
    }
    return string_new(vm, buffer, length);
}
