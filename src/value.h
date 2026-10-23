// Spectra3D Script - runtime value and object model
#ifndef SPS_VALUE_H
#define SPS_VALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Value Value;
typedef struct Obj Obj;
typedef struct VM VM;
typedef struct FunctionProto FunctionProto;
typedef struct ObjUpvalue ObjUpvalue;

// A native function. The user data is the one that was passed when the
// function was registered.
typedef Value (*NativeFn)(VM* vm, int argc, Value* args, void* user);

// A registered native function: the function and its user data.
typedef struct {
    NativeFn fn;
    void*    user;
} NativeEntry;

typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_INT,
    VAL_FLOAT,   // 64 bit, C++ "double"
    VAL_FLOAT32, // 32 bit, C++ "float"
    VAL_STRING,
    VAL_LIST,
    VAL_DICT,
    VAL_USERDATA,
    VAL_HOSTCLASS,
    VAL_OBJECT,  // an instance of a user defined class
    VAL_CLASS,   // a class itself
    VAL_CLOSURE,
    VAL_NATIVE
} ValueType;

struct Value {
    ValueType type;
    union {
        bool     boolean;
        int64_t  integer;
        double   number;
        Obj*     obj;
        void*    pointer;      // a class of the host application
        NativeEntry* native;
    } as;
};

typedef enum { OBJ_STRING, OBJ_LIST, OBJ_DICT, OBJ_ITERATOR, OBJ_CLASS, OBJ_INSTANCE,
                OBJ_USERDATA, OBJ_CLOSURE, OBJ_UPVALUE } ObjType;

struct Obj {
    ObjType type;
    Obj*    next;
};

typedef struct {
    Obj    obj;
    uint32_t len;
    uint32_t hash;
    char    chars[];
} ObjString;

typedef struct {
    Obj     obj;
    uint32_t count;
    uint32_t capacity;
    Value*  items;
} ObjList;

// Hash map with open addressing. Keys are strings or integers.
typedef struct {
    Value key;
    Value value;
    uint8_t state; // 0 = empty, 1 = used, 2 = deleted
} DictEntry;

typedef struct {
    Obj        obj;
    uint32_t   count;
    uint32_t   capacity;
    uint32_t   deleted;
    DictEntry* entries;
} ObjDict;

// Iterator used by the "for" statement.
typedef enum { ITER_LIST, ITER_STRING, ITER_DICT } IteratorKind;

typedef struct {
    Obj          obj;
    IteratorKind kind;
    Value        target;  // the list or the string being iterated
    ObjList*     keys;    // snapshot of the keys for dictionaries
    uint32_t     index;
    uint32_t     count;
} ObjIterator;

typedef struct {
    Obj            obj;
    FunctionProto* proto;
    uint8_t        upvalue_count;
    ObjUpvalue*    upvalues[];
} ObjClosure;

// A method of a class of the host application.
typedef struct {
    char*    name;
    NativeFn fn;
    void*    user;
} HostMethod;

// A property of a class of the host application: the getter receives the
// object, the setter receives the object and the new value.
typedef struct {
    char*    name;
    NativeFn getter;
    NativeFn setter;
    void*    user;
} HostProperty;

// A class of the host application: the engine exposes its own objects to
// the scripts through it.
typedef struct {
    char*       name;
    HostMethod* methods;
    int         method_count;
    int         method_cap;
    NativeFn    constructor;
    void*       constructor_user;
    void      (*free_fn)(void* data);
    void*       owner;    // the context that created the class
    HostProperty* properties;
    int         property_count;
    int         property_cap;
} HostClass;

// An object of the host application.
typedef struct {
    Obj        obj;
    void*      data;
    HostClass* klass;
} ObjUserdata;

// The cache of the method lookups: the class and the name give the index of
// the method. It saves the scan of the methods of the class on every call.
typedef struct MethodCacheEntry {
    void*  klass;
    const char* name;
    int    index;
} MethodCacheEntry;

// A user defined class: the field layout plus the methods.
typedef struct {
    Obj           obj;
    char*         name;
    bool          is_struct;
    uint32_t      field_count;
    char**        field_names;
    uint32_t      method_count;
    char**        method_names;
    ObjClosure**  methods;
    ObjClosure*   init;     // sets the default values of the fields
} ObjClass;

// An instance of a user defined class.
typedef struct {
    Obj        obj;
    ObjClass*  klass;
    uint32_t   field_count;
    Value*     fields;
} ObjInstance;

struct ObjUpvalue {
    Obj        obj;
    Value*     location; // pointer to the local slot while the upvalue is open
    Value      closed;   // saved value after the upvalue is closed
    bool       is_closed;
    ObjUpvalue* next;
};

static inline Value value_nil(void) {
    Value v;
    v.type = VAL_NIL;
    v.as.integer = 0;
    return v;
}

static inline Value value_bool(bool b) {
    Value v;
    v.type = VAL_BOOL;
    v.as.boolean = b;
    return v;
}

static inline Value value_int(int64_t i) {
    Value v;
    v.type = VAL_INT;
    v.as.integer = i;
    return v;
}

static inline Value value_float(double f) {
    Value v;
    v.type = VAL_FLOAT;
    v.as.number = f;
    return v;
}

// 32 bit float (C++ "float"): the value is rounded to float precision
static inline Value value_float32(double f) {
    Value v;
    v.type = VAL_FLOAT32;
    v.as.number = (double)(float)f;
    return v;
}

static inline Value value_obj(Obj* obj) {
    Value v;
    v.type = VAL_NIL;
    v.as.obj = obj;
    switch (obj->type) {
        case OBJ_STRING:   v.type = VAL_STRING;   break;
        case OBJ_LIST:     v.type = VAL_LIST;     break;
        case OBJ_DICT:     v.type = VAL_DICT;     break;
        case OBJ_CLASS:    v.type = VAL_CLASS;    break;
        case OBJ_USERDATA: v.type = VAL_USERDATA; break;
        case OBJ_INSTANCE: v.type = VAL_OBJECT;   break;
        case OBJ_CLOSURE:  v.type = VAL_CLOSURE;  break;
        default:           v.type = VAL_NIL;      break;
    }
    return v;
}

static inline bool value_is_number(Value v) {
    return v.type == VAL_INT || v.type == VAL_FLOAT || v.type == VAL_FLOAT32;
}

static inline bool value_is_float(Value v) {
    return v.type == VAL_FLOAT || v.type == VAL_FLOAT32;
}

// name of a type, used in diagnostics
const char* value_type_name(ValueType type);

// maps a type name ("int", "float", ...) to a type, false if it is not a type
bool type_from_name(const char* name, int length, ValueType* out);

static inline bool value_is_truthy(Value v) {
    if (v.type == VAL_NIL) return false;
    if (v.type == VAL_BOOL) return v.as.boolean;
    return true;
}

// --- comparison ---
bool values_equal(Value a, Value b);
bool values_compare(Value a, Value b, int* result); // -1/0/1; false if not comparable

// --- strings ---
ObjString* string_new(VM* vm, const char* chars, size_t len);
ObjString* string_new_cstr(VM* vm, const char* cstr);
ObjString* string_concat(VM* vm, ObjString* a, ObjString* b);
uint32_t   string_hash(const char* chars, size_t len);

// --- lists ---
ObjList* list_new(VM* vm);
void     list_push(VM* vm, ObjList* list, Value value);

// --- dictionaries ---
ObjDict* dict_new(VM* vm);
bool     dict_get(ObjDict* dict, Value key, Value* out);
void     dict_set(VM* vm, ObjDict* dict, Value key, Value value);
bool     dict_remove(ObjDict* dict, Value key);
void     dict_clear(ObjDict* dict);

// --- host objects ---
ObjUserdata* userdata_new(VM* vm, HostClass* klass, void* data);
HostClass*   host_class_new(const char* name);
int          host_class_add_method(HostClass* klass, const char* name, NativeFn fn, void* user);
int          host_class_find_method(HostClass* klass, const char* name);
int          host_class_add_property(HostClass* klass, const char* name, NativeFn getter,
                                     NativeFn setter, void* user);
int          host_class_find_property(HostClass* klass, const char* name);

// --- classes ---
ObjClass*    class_new(VM* vm, const char* name, bool is_struct);
ObjInstance* instance_new(VM* vm, ObjClass* klass);
int          class_find_field(ObjClass* klass, const char* name);
int          class_find_method(ObjClass* klass, const char* name);
ObjInstance* instance_copy(VM* vm, ObjInstance* source);

// --- iterators ---
ObjIterator* iterator_new(VM* vm, Value target);

// --- closures ---
ObjClosure* closure_new(VM* vm, FunctionProto* proto);

// --- memory ---
// There is no garbage collector: every heap object is released when the
// virtual machine is destroyed, so the process never leaks memory.
void mem_account(VM* vm, size_t bytes);
void mem_bytes(VM* vm, size_t bytes);
void objects_free_all(VM* vm);

// --- output / conversion ---
#include <stdio.h>
void        value_write(FILE* out, Value value);
ObjString*  string_from_value(VM* vm, Value value);

#endif /* SPS_VALUE_H */
