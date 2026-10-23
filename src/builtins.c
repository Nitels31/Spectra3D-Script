// Spectra3D Script - built-in functions
#include "builtins.h"
#include "dict.h"
#include "spslib.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Value builtin_print(VM* vm, int argc, Value* args, void* user) {
    (void)vm;
    for (int i = 0; i < argc; i++) {
        if (i > 0) fputc(' ', stdout);
        value_write(stdout, args[i]);
    }
    fputc('\n', stdout);
    return value_nil();
}

static Value builtin_len(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "len() expects exactly 1 argument");
    if (args[0].type == VAL_STRING) return value_int(((ObjString*)args[0].as.obj)->len);
    if (args[0].type == VAL_LIST) return value_int(((ObjList*)args[0].as.obj)->count);
    if (args[0].type == VAL_DICT) return value_int(((ObjDict*)args[0].as.obj)->count);
    vm_runtime_error(vm, 0, "len() is not supported for this type");
    return value_nil();
}

static Value builtin_str(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "str() expects exactly 1 argument");
    ObjString* result = string_from_value(vm, args[0]);
    return value_obj(&result->obj);
}

static Value builtin_int(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "int() expects exactly 1 argument");
    Value value = args[0];
    if (value.type == VAL_INT) return value;
    if (value.type == VAL_FLOAT) return value_int((int64_t)value.as.number);
    if (value.type == VAL_FLOAT32) return value_int((int64_t)value.as.number);
    if (value.type == VAL_BOOL) return value_int(value.as.boolean ? 1 : 0);
    if (value.type == VAL_STRING) {
        ObjString* str = (ObjString*)value.as.obj;
        char* buffer = (char*)malloc(str->len + 1);
        if (!buffer) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        memcpy(buffer, str->chars, str->len);
        buffer[str->len] = '\0';
        char* end = NULL;
        long long parsed = strtoll(buffer, &end, 10);
        free(buffer);
        if (end == buffer) vm_runtime_error(vm, 0, "int() cannot parse this string");
        return value_int((int64_t)parsed);
    }
    vm_runtime_error(vm, 0, "int() is not supported for this type");
    return value_nil();
}

static Value builtin_float(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "float() expects exactly 1 argument");
    Value value = args[0];
    if (value.type == VAL_FLOAT32) return value;
    if (value.type == VAL_FLOAT) return value_float32(value.as.number);
    if (value.type == VAL_INT) return value_float32((double)value.as.integer);
    if (value.type == VAL_STRING) {
        ObjString* str = (ObjString*)value.as.obj;
        char* buffer = (char*)malloc(str->len + 1);
        if (!buffer) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        memcpy(buffer, str->chars, str->len);
        buffer[str->len] = '\0';
        char* end = NULL;
        double parsed = strtod(buffer, &end);
        free(buffer);
        if (end == buffer) vm_runtime_error(vm, 0, "float() cannot parse this string");
        return value_float32(parsed);
    }
    vm_runtime_error(vm, 0, "float() is not supported for this type");
    return value_nil();
}

static Value builtin_double(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "double() expects exactly 1 argument");
    Value value = args[0];
    if (value.type == VAL_FLOAT) return value;
    if (value.type == VAL_FLOAT32) return value_float(value.as.number);
    if (value.type == VAL_INT) return value_float((double)value.as.integer);
    if (value.type == VAL_STRING) {
        ObjString* str = (ObjString*)value.as.obj;
        char* buffer = (char*)malloc(str->len + 1);
        if (!buffer) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        memcpy(buffer, str->chars, str->len);
        buffer[str->len] = '\0';
        char* end = NULL;
        double parsed = strtod(buffer, &end);
        free(buffer);
        if (end == buffer) vm_runtime_error(vm, 0, "double() cannot parse this string");
        return value_float(parsed);
    }
    vm_runtime_error(vm, 0, "double() is not supported for this type");
    return value_nil();
}

static Value builtin_bool(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "bool() expects exactly 1 argument");
    return value_bool(value_is_truthy(args[0]));
}

static Value builtin_type(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "type() expects exactly 1 argument");
    const char* name = "nil";
    switch (args[0].type) {
        case VAL_NIL:     name = "nil"; break;
        case VAL_BOOL:    name = "bool"; break;
        case VAL_INT:     name = "int"; break;
        case VAL_FLOAT:   name = "double"; break;
        case VAL_FLOAT32: name = "float"; break;
        case VAL_STRING:  name = "str"; break;
        case VAL_LIST:    name = "list"; break;
        case VAL_DICT:    name = "dict"; break;
        case VAL_OBJECT:  name = "object"; break;
        case VAL_USERDATA: name = "object"; break;
        case VAL_HOSTCLASS: name = "class"; break;
        case VAL_CLASS:   name = "class"; break;
        case VAL_CLOSURE: name = "fn"; break;
        case VAL_NATIVE:  name = "fn"; break;
    }
    ObjString* result = string_new_cstr(vm, name);
    return value_obj(&result->obj);
}

static Value builtin_input(VM* vm, int argc, Value* args, void* user) {
    if (argc > 1) vm_runtime_error(vm, 0, "input() expects at most 1 argument");
    if (argc == 1 && args[0].type == VAL_STRING) {
        ObjString* prompt = (ObjString*)args[0].as.obj;
        fwrite(prompt->chars, 1, prompt->len, stdout);
    }
    fflush(stdout);

    size_t capacity = 64;
    size_t length = 0;
    char* buffer = (char*)malloc(capacity);
    if (!buffer) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    int c;
    while ((c = getchar()) != EOF && c != '\n') {
        if (length + 1 >= capacity) {
            capacity *= 2;
            char* grown = (char*)realloc(buffer, capacity);
            if (!grown) {
                free(buffer);
                fprintf(stderr, "sps: out of memory\n");
                exit(1);
            }
            buffer = grown;
        }
        buffer[length++] = (char)c;
    }
    ObjString* result = string_new(vm, buffer, length);
    free(buffer);
    return value_obj(&result->obj);
}

static Value builtin_range(VM* vm, int argc, Value* args, void* user) {
    if (argc < 1 || argc > 3) vm_runtime_error(vm, 0, "range() expects 1 to 3 arguments");

    int64_t start = 0;
    int64_t stop = 0;
    int64_t step = 1;

    if (argc == 1) {
        stop = args[0].as.integer;
    } else {
        start = args[0].as.integer;
        stop = args[1].as.integer;
        if (argc == 3) step = args[2].as.integer;
    }
    if (argc == 1 && args[0].type != VAL_INT) vm_runtime_error(vm, 0, "range() expects integers");
    if (step == 0) vm_runtime_error(vm, 0, "range() step must not be zero");

    ObjList* list = list_new(vm);
    if (step > 0) {
        for (int64_t i = start; i < stop; i += step) list_push(vm, list, value_int(i));
    } else {
        for (int64_t i = start; i > stop; i += step) list_push(vm, list, value_int(i));
    }
    return value_obj(&list->obj);
}

static Value builtin_abs(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "abs() expects exactly 1 argument");
    if (args[0].type == VAL_INT) {
        int64_t value = args[0].as.integer;
        return value_int(value < 0 ? -value : value);
    }
    if (args[0].type == VAL_FLOAT) {
        double value = args[0].as.number;
        return value_float(value < 0 ? -value : value);
    }
    vm_runtime_error(vm, 0, "abs() expects a number");
    return value_nil();
}

static bool list_of_numbers(VM* vm, Value value, ObjList** out) {
    if (value.type != VAL_LIST) {
        vm_runtime_error(vm, 0, "expected a list of numbers");
        return false;
    }
    *out = (ObjList*)value.as.obj;
    return true;
}

static Value builtin_min(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "min() expects exactly 1 argument");
    ObjList* list = NULL;
    list_of_numbers(vm, args[0], &list);
    if (list->count == 0) vm_runtime_error(vm, 0, "min() of an empty list");
    Value best = list->items[0];
    for (uint32_t i = 1; i < list->count; i++) {
        int cmp = 0;
        if (!values_compare(list->items[i], best, &cmp)) {
            vm_runtime_error(vm, 0, "min() requires comparable values");
        }
        if (cmp < 0) best = list->items[i];
    }
    return best;
}

static Value builtin_max(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "max() expects exactly 1 argument");
    ObjList* list = NULL;
    list_of_numbers(vm, args[0], &list);
    if (list->count == 0) vm_runtime_error(vm, 0, "max() of an empty list");
    Value best = list->items[0];
    for (uint32_t i = 1; i < list->count; i++) {
        int cmp = 0;
        if (!values_compare(list->items[i], best, &cmp)) {
            vm_runtime_error(vm, 0, "max() requires comparable values");
        }
        if (cmp > 0) best = list->items[i];
    }
    return best;
}

static Value builtin_sum(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "sum() expects exactly 1 argument");
    ObjList* list = NULL;
    list_of_numbers(vm, args[0], &list);
    bool is_float = false;
    double total = 0.0;
    int64_t integer_total = 0;
    for (uint32_t i = 0; i < list->count; i++) {
        Value item = list->items[i];
        if (item.type == VAL_FLOAT) is_float = true;
        else if (item.type != VAL_INT) vm_runtime_error(vm, 0, "sum() requires numbers");
        if (item.type == VAL_INT) integer_total += item.as.integer;
        total += (item.type == VAL_INT ? (double)item.as.integer : item.as.number);
    }
    return is_float ? value_float(total) : value_int(integer_total);
}

static Value builtin_ord(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1 || args[0].type != VAL_STRING) {
        vm_runtime_error(vm, 0, "ord() expects a single character string");
    }
    ObjString* str = (ObjString*)args[0].as.obj;
    if (str->len != 1) vm_runtime_error(vm, 0, "ord() expects a single character string");
    return value_int((unsigned char)str->chars[0]);
}

static Value builtin_chr(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1 || args[0].type != VAL_INT) {
        vm_runtime_error(vm, 0, "chr() expects a single integer");
    }
    char c = (char)args[0].as.integer;
    ObjString* result = string_new(vm, &c, 1);
    return value_obj(&result->obj);
}

const BuiltinDef SPS_BUILTINS[] = {
    { "print",  builtin_print },
    { "len",    builtin_len },
    { "str",    builtin_str },
    { "int",    builtin_int },
    { "float",  builtin_float },
    { "double", builtin_double },
    { "bool",   builtin_bool },
    { "type",   builtin_type },
    { "input",  builtin_input },
    { "range",  builtin_range },
    { "abs",    builtin_abs },
    { "min",    builtin_min },
    { "max",    builtin_max },
    { "sum",    builtin_sum },
    { "ord",    builtin_ord },
    { "chr",    builtin_chr },

    // the standard library
    { "read_file",     sps_read_file },
    { "write_file",    sps_write_file },
    { "append_file",   sps_append_file },
    { "file_exists",   sps_file_exists },
    { "delete_file",   sps_delete_file },
    { "file_size",     sps_file_size },
    { "list_dir",      sps_list_dir },
    { "make_dir",      sps_make_dir },
    { "time",          sps_time },
    { "now",           sps_now },
    { "sleep",         sps_sleep },
    { "json_parse",    sps_json_parse },
    { "json_stringify", sps_json_stringify },
    { "json_pretty",   sps_json_pretty },

    // the compact format of Spectra3D
    { "spl_parse",     spl_builtin_parse },
    { "spl_stringify", spl_builtin_stringify },
    { "spl_pretty",    spl_builtin_pretty },
    { "spl_load",      spl_builtin_load },
    { "spl_save",      spl_builtin_save },

    // the collections, the formats and the assertions
    { "zip",        sps_zip },
    { "unique",     sps_unique },
    { "flatten",    sps_flatten },
    { "sortBy",     sps_sort_by },
    { "merge",      sps_merge },
    { "deepCopy",   sps_deep_copy },
    { "first",      sps_first },
    { "last",       sps_last },
    { "take",       sps_take },
    { "skip",       sps_skip },
    { "indexOf",    sps_index_of },
    { "toHex",      sps_to_hex },
    { "toBinary",   sps_to_binary },
    { "toFixed",    sps_to_fixed },
    { "padStart",   sps_pad_start },
    { "padEnd",     sps_pad_end },
    { "charCode",   sps_char_code },
    { "fromCharCode", sps_from_char_code },
    { "assert",     sps_assert },
    { "assertEqual", sps_assert_equal },

    // the regular expressions
    { "regex",         sps_regex_new },
    { "regexMatch",    sps_regex_match },
    { "regexTest",     sps_regex_test },
    { "regexSearch",   sps_regex_search },
    { "regexReplace",  sps_regex_replace },
    { "regexSplit",    sps_regex_split },
    { "regexGroupCount", sps_regex_group_count },

    // the result
    { "resultOk",     sps_result_ok },
    { "resultError",  sps_result_error },
    { "isOk",         sps_is_ok },
    { "isError",      sps_is_error },
    { "unwrap",       sps_unwrap },
    { "unwrapOr",     sps_unwrap_or },

    // the functions of the collections
    { "map",           sps_map },
    { "filter",        sps_filter },
    { "reduce",        sps_reduce },
    { "sqrt",          sps_sqrt },
    { "sin",           sps_sin },
    { "cos",           sps_cos },
    { "tan",           sps_tan },
    { "atan2",         sps_atan2 },
    { "pow",           sps_pow },
    { "floor",         sps_floor },
    { "ceil",          sps_ceil },
    { "round",         sps_round },
    { "random",        sps_random },
    { "random_range",  sps_random_range },

    // formatting
    { "format",        sps_builtin_format }
};

const int SPS_BUILTIN_COUNT = (int)(sizeof(SPS_BUILTINS) / sizeof(SPS_BUILTINS[0]));
