// Spectra3D Script - methods of the built-in types
//
// A method is a native function that receives the receiver as the first
// argument: fn(vm, argc, args) where args[0] is the receiver and
// args[1..argc) are the arguments of the call.

#include "methods.h"
#include "dict.h"
#include "spslib.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------
// helpers
// ------------------------------------------------------------------

static ObjString* receiver_string(VM* vm, Value receiver, const char* method) {
    if (receiver.type != VAL_STRING) {
        vm_runtime_error(vm, 0, "'%s' expects a string", method);
    }
    return (ObjString*)receiver.as.obj;
}

static ObjList* receiver_list(VM* vm, Value receiver, const char* method) {
    if (receiver.type != VAL_LIST) {
        vm_runtime_error(vm, 0, "'%s' expects a list", method);
    }
    return (ObjList*)receiver.as.obj;
}

static ObjDict* receiver_dict(VM* vm, Value receiver, const char* method) {
    if (receiver.type != VAL_DICT) {
        vm_runtime_error(vm, 0, "'%s' expects a dictionary", method);
    }
    return (ObjDict*)receiver.as.obj;
}

// ------------------------------------------------------------------
// string methods
// ------------------------------------------------------------------

static Value string_upper(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    ObjString* str = receiver_string(vm, args[0], "upper");
    char* buffer = (char*)malloc(str->len + 1);
    if (!buffer) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    for (uint32_t i = 0; i < str->len; i++) {
        char c = str->chars[i];
        buffer[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    ObjString* result = string_new(vm, buffer, str->len);
    free(buffer);
    return value_obj(&result->obj);
}

static Value string_lower(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    ObjString* str = receiver_string(vm, args[0], "lower");
    char* buffer = (char*)malloc(str->len + 1);
    if (!buffer) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    for (uint32_t i = 0; i < str->len; i++) {
        char c = str->chars[i];
        buffer[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    ObjString* result = string_new(vm, buffer, str->len);
    free(buffer);
    return value_obj(&result->obj);
}

static Value string_strip(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    ObjString* str = receiver_string(vm, args[0], "strip");
    uint32_t start = 0;
    uint32_t end = str->len;
    while (start < end && (str->chars[start] == ' ' || str->chars[start] == '\t' ||
                           str->chars[start] == '\n' || str->chars[start] == '\r')) {
        start++;
    }
    while (end > start && (str->chars[end - 1] == ' ' || str->chars[end - 1] == '\t' ||
                           str->chars[end - 1] == '\n' || str->chars[end - 1] == '\r')) {
        end--;
    }
    ObjString* result = string_new(vm, str->chars + start, end - start);
    return value_obj(&result->obj);
}

static Value string_split(VM* vm, int argc, Value* args, void* user) {
    if (argc < 2) vm_runtime_error(vm, 0, "'split' expects 1 argument");
    ObjString* str = receiver_string(vm, args[0], "split");
    ObjString* separator = receiver_string(vm, args[1], "split");
    if (separator->len == 0) vm_runtime_error(vm, 0, "'split' separator must not be empty");

    ObjList* list = list_new(vm);
    uint32_t start = 0;
    for (uint32_t i = 0; i + separator->len <= str->len;) {
        if (memcmp(str->chars + i, separator->chars, separator->len) == 0) {
            ObjString* part = string_new(vm, str->chars + start, i - start);
            list_push(vm, list, value_obj(&part->obj));
            i += separator->len;
            start = i;
            continue;
        }
        i++;
    }
    ObjString* tail = string_new(vm, str->chars + start, str->len - start);
    list_push(vm, list, value_obj(&tail->obj));
    return value_obj(&list->obj);
}

static Value string_replace(VM* vm, int argc, Value* args, void* user) {
    if (argc < 3) vm_runtime_error(vm, 0, "'replace' expects 2 arguments");
    ObjString* str = receiver_string(vm, args[0], "replace");
    ObjString* from = receiver_string(vm, args[1], "replace");
    ObjString* to = receiver_string(vm, args[2], "replace");
    if (from->len == 0) vm_runtime_error(vm, 0, "'replace' pattern must not be empty");

    size_t capacity = str->len + 1;
    char* buffer = (char*)malloc(capacity);
    if (!buffer) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    size_t length = 0;
    for (uint32_t i = 0; i < str->len;) {
        if (i + from->len <= str->len && memcmp(str->chars + i, from->chars, from->len) == 0) {
            memcpy(buffer + length, to->chars, to->len);
            length += to->len;
            i += from->len;
            continue;
        }
        buffer[length++] = str->chars[i++];
    }
    ObjString* result = string_new(vm, buffer, length);
    free(buffer);
    return value_obj(&result->obj);
}

static Value string_find(VM* vm, int argc, Value* args, void* user) {
    if (argc < 2) vm_runtime_error(vm, 0, "'find' expects 1 argument");
    ObjString* str = receiver_string(vm, args[0], "find");
    ObjString* needle = receiver_string(vm, args[1], "find");
    if (needle->len == 0) return value_int(0);
    for (uint32_t i = 0; i + needle->len <= str->len; i++) {
        if (memcmp(str->chars + i, needle->chars, needle->len) == 0) {
            return value_int((int64_t)i);
        }
    }
    return value_int(-1);
}

static Value string_startswith(VM* vm, int argc, Value* args, void* user) {
    if (argc < 2) vm_runtime_error(vm, 0, "'startswith' expects 1 argument");
    ObjString* str = receiver_string(vm, args[0], "startswith");
    ObjString* prefix = receiver_string(vm, args[1], "startswith");
    if (prefix->len > str->len) return value_bool(false);
    return value_bool(memcmp(str->chars, prefix->chars, prefix->len) == 0);
}

static Value string_endswith(VM* vm, int argc, Value* args, void* user) {
    if (argc < 2) vm_runtime_error(vm, 0, "'endswith' expects 1 argument");
    ObjString* str = receiver_string(vm, args[0], "endswith");
    ObjString* suffix = receiver_string(vm, args[1], "endswith");
    if (suffix->len > str->len) return value_bool(false);
    return value_bool(memcmp(str->chars + str->len - suffix->len, suffix->chars, suffix->len) == 0);
}

static Value string_contains(VM* vm, int argc, Value* args, void* user) {
    if (argc < 2) vm_runtime_error(vm, 0, "'contains' expects 1 argument");
    ObjString* str = receiver_string(vm, args[0], "contains");
    ObjString* needle = receiver_string(vm, args[1], "contains");
    if (needle->len == 0) return value_bool(true);
    for (uint32_t i = 0; i + needle->len <= str->len; i++) {
        if (memcmp(str->chars + i, needle->chars, needle->len) == 0) {
            return value_bool(true);
        }
    }
    return value_bool(false);
}

static Value string_count(VM* vm, int argc, Value* args, void* user) {
    if (argc < 2) vm_runtime_error(vm, 0, "'count' expects 1 argument");
    ObjString* str = receiver_string(vm, args[0], "count");
    ObjString* needle = receiver_string(vm, args[1], "count");
    if (needle->len == 0) return value_int(0);
    int64_t count = 0;
    for (uint32_t i = 0; i + needle->len <= str->len;) {
        if (memcmp(str->chars + i, needle->chars, needle->len) == 0) {
            count++;
            i += needle->len;
            continue;
        }
        i++;
    }
    return value_int(count);
}

static Value string_isdigit(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    ObjString* str = receiver_string(vm, args[0], "isdigit");
    if (str->len == 0) return value_bool(false);
    for (uint32_t i = 0; i < str->len; i++) {
        if (str->chars[i] < '0' || str->chars[i] > '9') return value_bool(false);
    }
    return value_bool(true);
}

static Value string_isalpha(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    ObjString* str = receiver_string(vm, args[0], "isalpha");
    if (str->len == 0) return value_bool(false);
    for (uint32_t i = 0; i < str->len; i++) {
        char c = str->chars[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return value_bool(false);
    }
    return value_bool(true);
}

static Value string_isspace(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    ObjString* str = receiver_string(vm, args[0], "isspace");
    if (str->len == 0) return value_bool(false);
    for (uint32_t i = 0; i < str->len; i++) {
        char c = str->chars[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return value_bool(false);
    }
    return value_bool(true);
}

// The method "length()" of the strings, the lists and the dictionaries.
static Value string_length(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    (void)user;
    ObjString* str = receiver_string(vm, args[0], "length");
    return value_int(str->len);
}

static Value list_length(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    (void)user;
    ObjList* list = receiver_list(vm, args[0], "length");
    return value_int((int64_t)list->count);
}

static Value dict_length(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    (void)user;
    ObjDict* dict = receiver_dict(vm, args[0], "length");
    return value_int((int64_t)dict->count);
}

// ------------------------------------------------------------------
// list methods
// ------------------------------------------------------------------

static Value list_append(VM* vm, int argc, Value* args, void* user) {
    if (argc < 2) vm_runtime_error(vm, 0, "'append' expects 1 argument");
    ObjList* list = receiver_list(vm, args[0], "append");
    list_push(vm, list, args[1]);
    return value_nil();
}

static Value list_pop(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    ObjList* list = receiver_list(vm, args[0], "pop");
    if (list->count == 0) vm_runtime_error(vm, 0, "'pop' on an empty list");
    Value result = list->items[--list->count];
    return result;
}

static Value list_insert(VM* vm, int argc, Value* args, void* user) {
    if (argc < 3) vm_runtime_error(vm, 0, "'insert' expects 2 arguments");
    ObjList* list = receiver_list(vm, args[0], "insert");
    if (args[1].type != VAL_INT) vm_runtime_error(vm, 0, "'insert' index must be an integer");
    int64_t position = args[1].as.integer;
    if (position < 0) position += (int64_t)list->count;
    if (position < 0 || position > (int64_t)list->count) {
        vm_runtime_error(vm, 0, "'insert' index out of range");
    }
    list_push(vm, list, value_nil());
    for (int64_t i = (int64_t)list->count - 1; i > position; i--) {
        list->items[i] = list->items[i - 1];
    }
    list->items[position] = args[2];
    return value_nil();
}

static Value list_remove(VM* vm, int argc, Value* args, void* user) {
    if (argc < 2) vm_runtime_error(vm, 0, "'remove' expects 1 argument");
    ObjList* list = receiver_list(vm, args[0], "remove");
    for (uint32_t i = 0; i < list->count; i++) {
        if (values_equal(list->items[i], args[1])) {
            for (uint32_t j = i; j + 1 < list->count; j++) {
                list->items[j] = list->items[j + 1];
            }
            list->count--;
            return value_nil();
        }
    }
    vm_runtime_error(vm, 0, "'remove': value not found");
    return value_nil();
}

static Value list_index(VM* vm, int argc, Value* args, void* user) {
    if (argc < 2) vm_runtime_error(vm, 0, "'index' expects 1 argument");
    ObjList* list = receiver_list(vm, args[0], "index");
    for (uint32_t i = 0; i < list->count; i++) {
        if (values_equal(list->items[i], args[1])) return value_int((int64_t)i);
    }
    return value_int(-1);
}

static Value list_count(VM* vm, int argc, Value* args, void* user) {
    if (argc < 2) vm_runtime_error(vm, 0, "'count' expects 1 argument");
    ObjList* list = receiver_list(vm, args[0], "count");
    int64_t count = 0;
    for (uint32_t i = 0; i < list->count; i++) {
        if (values_equal(list->items[i], args[1])) count++;
    }
    return value_int(count);
}

static Value list_contains(VM* vm, int argc, Value* args, void* user) {
    if (argc < 2) vm_runtime_error(vm, 0, "'contains' expects 1 argument");
    ObjList* list = receiver_list(vm, args[0], "contains");
    for (uint32_t i = 0; i < list->count; i++) {
        if (values_equal(list->items[i], args[1])) return value_bool(true);
    }
    return value_bool(false);
}

static Value list_reverse(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    ObjList* list = receiver_list(vm, args[0], "reverse");
    for (uint32_t i = 0, j = list->count ? list->count - 1 : 0; i < j; i++, j--) {
        Value tmp = list->items[i];
        list->items[i] = list->items[j];
        list->items[j] = tmp;
    }
    return value_nil();
}

static int compare_values(const void* a, const void* b) {
    Value x = *(const Value*)a;
    Value y = *(const Value*)b;
    int result = 0;
    if (!values_compare(x, y, &result)) return 0;
    return result;
}

static Value list_sort(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    ObjList* list = receiver_list(vm, args[0], "sort");
    if (list->count > 1) {
        qsort(list->items, list->count, sizeof(Value), compare_values);
    }
    return value_nil();
}

static Value list_clear(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    ObjList* list = receiver_list(vm, args[0], "clear");
    list->count = 0;
    return value_nil();
}

static Value list_join(VM* vm, int argc, Value* args, void* user) {
    if (argc < 2) vm_runtime_error(vm, 0, "'join' expects 1 argument");
    ObjList* list = receiver_list(vm, args[0], "join");
    ObjString* separator = receiver_string(vm, args[1], "join");

    size_t capacity = 16;
    char* buffer = (char*)malloc(capacity);
    if (!buffer) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    size_t length = 0;
    for (uint32_t i = 0; i < list->count; i++) {
        if (list->items[i].type != VAL_STRING) {
            free(buffer);
            vm_runtime_error(vm, 0, "'join' expects a list of strings");
        }
        ObjString* item = (ObjString*)list->items[i].as.obj;
        while (length + item->len + separator->len + 1 > capacity) {
            capacity *= 2;
            char* grown = (char*)realloc(buffer, capacity);
            if (!grown) {
                free(buffer);
                fprintf(stderr, "sps: out of memory\n");
                exit(1);
            }
            buffer = grown;
        }
        memcpy(buffer + length, item->chars, item->len);
        length += item->len;
        if (i + 1 < list->count) {
            memcpy(buffer + length, separator->chars, separator->len);
            length += separator->len;
        }
    }
    ObjString* result = string_new(vm, buffer, length);
    free(buffer);
    return value_obj(&result->obj);
}

// ------------------------------------------------------------------
// dictionary methods
// ------------------------------------------------------------------

static Value method_dict_get(VM* vm, int argc, Value* args, void* user) {
    if (argc < 2) vm_runtime_error(vm, 0, "'get' expects 1 argument");
    ObjDict* dict = receiver_dict(vm, args[0], "get");
    Value value;
    if (dict_get(dict, args[1], &value)) return value;
    if (argc >= 3) return args[2];
    return value_nil();
}

static Value method_dict_has(VM* vm, int argc, Value* args, void* user) {
    if (argc < 2) vm_runtime_error(vm, 0, "'has' expects 1 argument");
    ObjDict* dict = receiver_dict(vm, args[0], "has");
    Value value;
    return value_bool(dict_get(dict, args[1], &value));
}

static Value method_dict_remove(VM* vm, int argc, Value* args, void* user) {
    if (argc < 2) vm_runtime_error(vm, 0, "'remove' expects 1 argument");
    ObjDict* dict = receiver_dict(vm, args[0], "remove");
    Value removed;
    if (!dict_get(dict, args[1], &removed)) return value_nil();
    dict_remove(dict, args[1]);
    return removed;
}

static Value method_dict_keys(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    ObjDict* dict = receiver_dict(vm, args[0], "keys");
    ObjList* list = list_new(vm);
    for (uint32_t i = 0; i < dict->capacity; i++) {
        if (dict->entries[i].state == 1) list_push(vm, list, dict->entries[i].key);
    }
    return value_obj(&list->obj);
}

static Value method_dict_values(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    ObjDict* dict = receiver_dict(vm, args[0], "values");
    ObjList* list = list_new(vm);
    for (uint32_t i = 0; i < dict->capacity; i++) {
        if (dict->entries[i].state == 1) list_push(vm, list, dict->entries[i].value);
    }
    return value_obj(&list->obj);
}

static Value method_dict_items(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    ObjDict* dict = receiver_dict(vm, args[0], "items");
    ObjList* list = list_new(vm);
    for (uint32_t i = 0; i < dict->capacity; i++) {
        if (dict->entries[i].state != 1) continue;
        ObjList* pair = list_new(vm);
        list_push(vm, pair, dict->entries[i].key);
        list_push(vm, pair, dict->entries[i].value);
        list_push(vm, list, value_obj(&pair->obj));
    }
    return value_obj(&list->obj);
}

static Value method_dict_clear(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    ObjDict* dict = receiver_dict(vm, args[0], "clear");
    dict_clear(dict);
    return value_nil();
}

// ------------------------------------------------------------------
// the method table
// ------------------------------------------------------------------

const MethodDef SPS_METHODS[] = {
    // strings
    { "upper",      VAL_STRING, string_upper },
    { "format",     VAL_STRING, sps_method_format },
    { "lower",      VAL_STRING, string_lower },
    { "strip",      VAL_STRING, string_strip },
    { "split",      VAL_STRING, string_split },
    { "replace",    VAL_STRING, string_replace },
    { "find",       VAL_STRING, string_find },
    { "startswith", VAL_STRING, string_startswith },
    { "endswith",   VAL_STRING, string_endswith },
    { "contains",   VAL_STRING, string_contains },
    { "count",      VAL_STRING, string_count },
    { "isdigit",    VAL_STRING, string_isdigit },
    { "isalpha",    VAL_STRING, string_isalpha },
    { "isspace",    VAL_STRING, string_isspace },
    { "length",     VAL_STRING, string_length },

    // lists
    { "append",     VAL_LIST, list_append },
    { "pop",        VAL_LIST, list_pop },
    { "insert",     VAL_LIST, list_insert },
    { "remove",     VAL_LIST, list_remove },
    { "index",      VAL_LIST, list_index },
    { "count",      VAL_LIST, list_count },
    { "contains",   VAL_LIST, list_contains },
    { "reverse",    VAL_LIST, list_reverse },
    { "sort",       VAL_LIST, list_sort },
    { "clear",      VAL_LIST, list_clear },
    { "join",       VAL_LIST, list_join },
    { "length",     VAL_LIST, list_length },

    // dictionaries
    { "get",        VAL_DICT, method_dict_get },
    { "has",        VAL_DICT, method_dict_has },
    { "remove",     VAL_DICT, method_dict_remove },
    { "keys",       VAL_DICT, method_dict_keys },
    { "values",     VAL_DICT, method_dict_values },
    { "items",      VAL_DICT, method_dict_items },
    { "clear",      VAL_DICT, method_dict_clear },
    { "length",     VAL_DICT, dict_length }
};

const int SPS_METHOD_COUNT = (int)(sizeof(SPS_METHODS) / sizeof(SPS_METHODS[0]));

// ------------------------------------------------------------------
// method groups
// ------------------------------------------------------------------

#define MAX_GROUPS 64

typedef struct {
    char*    name;
    NativeFn impl[VAL_NATIVE + 1];
} MethodGroup;

static MethodGroup groups[MAX_GROUPS];
static int group_count = 0;
static bool groups_ready = false;

void methods_init(void) {
    if (groups_ready) return;
    groups_ready = true;
    group_count = 0;

    for (int i = 0; i < SPS_METHOD_COUNT; i++) {
        const MethodDef* method = &SPS_METHODS[i];
        int group = -1;
        for (int g = 0; g < group_count; g++) {
            if (strcmp(groups[g].name, method->name) == 0) {
                group = g;
                break;
            }
        }
        if (group < 0) {
            if (group_count >= MAX_GROUPS) {
                fprintf(stderr, "sps: too many method groups\n");
                exit(1);
            }
            group = group_count++;
            groups[group].name = (char*)method->name;
        }
        groups[group].impl[method->type] = method->fn;
    }
}

int method_group_count(void) {
    methods_init();
    return group_count;
}

int method_group_find(const char* name, int length) {
    methods_init();
    for (int g = 0; g < group_count; g++) {
        if ((int)strlen(groups[g].name) == length &&
            memcmp(groups[g].name, name, (size_t)length) == 0) {
            return g;
        }
    }
    return -1;
}

const char* method_group_name(int group) {
    methods_init();
    if (group < 0 || group >= group_count) return "?";
    return groups[group].name;
}

NativeFn method_lookup(int group, ValueType type) {
    methods_init();
    if (group < 0 || group >= group_count) return NULL;
    if ((int)type < 0 || (int)type > VAL_NATIVE) return NULL;
    return groups[group].impl[type];
}
