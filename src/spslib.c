// Spectra3D Script - standard library (files, time, json, math)

#include "spslib.h"
#include "dict.h"
#include "regex.h"
#include "spl.h"
#include "vm.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#endif

// ------------------------------------------------------------------
// helpers
// ------------------------------------------------------------------

static int64_t arg_int(VM* vm, Value value, const char* function) {
    if (value.type == VAL_INT) return value.as.integer;
    if (value.type == VAL_FLOAT) return (int64_t)value.as.number;
    if (value.type == VAL_BOOL) return value.as.boolean ? 1 : 0;
    vm_runtime_error(vm, 0, "'%s' expects a number", function);
    return 0;
}

static double arg_double(VM* vm, Value value, const char* function) {
    if (value.type == VAL_INT) return (double)value.as.integer;
    if (value.type == VAL_FLOAT || value.type == VAL_FLOAT32) return value.as.number;
    vm_runtime_error(vm, 0, "'%s' expects a number", function);
    return 0.0;
}

static const char* arg_string(VM* vm, Value value, const char* function) {
    if (value.type != VAL_STRING) {
        vm_runtime_error(vm, 0, "'%s' expects a string", function);
    }
    return ((ObjString*)value.as.obj)->chars;
}

static double arg_number(VM* vm, Value value, const char* function) {
    if (value.type == VAL_INT) return (double)value.as.integer;
    if (value.type == VAL_FLOAT || value.type == VAL_FLOAT32) return value.as.number;
    vm_runtime_error(vm, 0, "'%s' expects a number", function);
    return 0.0;
}

// ------------------------------------------------------------------
// files
// ------------------------------------------------------------------

Value sps_read_file(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "read_file() expects 1 argument");
    const char* path = arg_string(vm, args[0], "read_file");

    FILE* file = fopen(path, "rb");
    if (!file) return value_nil();

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) size = 0;

    char* buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        vm_runtime_error(vm, 0, "out of memory");
    }
    size_t read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[read] = '\0';

    ObjString* result = string_new(vm, buffer, read);
    free(buffer);
    return value_obj(&result->obj);
}

Value sps_write_file(VM* vm, int argc, Value* args, void* user) {
    if (argc != 2) vm_runtime_error(vm, 0, "write_file() expects 2 arguments");
    const char* path = arg_string(vm, args[0], "write_file");
    const char* content = arg_string(vm, args[1], "write_file");

    FILE* file = fopen(path, "wb");
    if (!file) return value_bool(false);
    size_t length = strlen(content);
    fwrite(content, 1, length, file);
    fclose(file);
    return value_bool(true);
}

Value sps_append_file(VM* vm, int argc, Value* args, void* user) {
    if (argc != 2) vm_runtime_error(vm, 0, "append_file() expects 2 arguments");
    const char* path = arg_string(vm, args[0], "append_file");
    const char* content = arg_string(vm, args[1], "append_file");

    FILE* file = fopen(path, "ab");
    if (!file) return value_bool(false);
    fwrite(content, 1, strlen(content), file);
    fclose(file);
    return value_bool(true);
}

Value sps_file_exists(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "file_exists() expects 1 argument");
    const char* path = arg_string(vm, args[0], "file_exists");
    FILE* file = fopen(path, "rb");
    if (!file) return value_bool(false);
    fclose(file);
    return value_bool(true);
}

Value sps_delete_file(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "delete_file() expects 1 argument");
    const char* path = arg_string(vm, args[0], "delete_file");
    return value_bool(remove(path) == 0);
}

Value sps_file_size(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "file_size() expects 1 argument");
    const char* path = arg_string(vm, args[0], "file_size");
    FILE* file = fopen(path, "rb");
    if (!file) return value_int(-1);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    return value_int((int64_t)size);
}

Value sps_list_dir(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "list_dir() expects 1 argument");
    const char* path = arg_string(vm, args[0], "list_dir");

    ObjList* list = list_new(vm);
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s/*", path);
    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA(pattern, &data);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
            ObjString* name = string_new_cstr(vm, data.cFileName);
            list_push(vm, list, value_obj(&name->obj));
        } while (FindNextFileA(handle, &data));
        FindClose(handle);
    }
#else
    DIR* dir = opendir(path);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            ObjString* name = string_new_cstr(vm, entry->d_name);
            list_push(vm, list, value_obj(&name->obj));
        }
        closedir(dir);
    }
#endif
    return value_obj(&list->obj);
}

Value sps_make_dir(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "make_dir() expects 1 argument");
    const char* path = arg_string(vm, args[0], "make_dir");
#ifdef _WIN32
    return value_bool(CreateDirectoryA(path, NULL) != 0);
#else
    return value_bool(mkdir(path, 0755) == 0);
#endif
}

// ------------------------------------------------------------------
// time
// ------------------------------------------------------------------

Value sps_time(VM* vm, int argc, Value* args, void* user) {
    (void)vm;
    (void)argc;
    (void)args;
    return value_float((double)time(NULL));
}

Value sps_now(VM* vm, int argc, Value* args, void* user) {
    (void)argc;
    (void)args;
    time_t seconds = time(NULL);
    struct tm local;
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    ObjString* result = string_new_cstr(vm, buffer);
    return value_obj(&result->obj);
}

Value sps_sleep(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "sleep() expects 1 argument");
    double milliseconds = arg_number(vm, args[0], "sleep");
    if (milliseconds < 0) milliseconds = 0;
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec request;
    request.tv_sec = (time_t)(milliseconds / 1000.0);
    request.tv_nsec = (long)((milliseconds - request.tv_sec * 1000.0) * 1000000.0);
    nanosleep(&request, NULL);
#endif
    return value_nil();
}

// ------------------------------------------------------------------
// json
// ------------------------------------------------------------------

typedef struct {
    const char* text;
    size_t      position;
    VM*         vm;
    bool        failed;
} JsonParser;

static void json_skip_space(JsonParser* parser) {
    while (parser->text[parser->position] &&
           isspace((unsigned char)parser->text[parser->position])) {
        parser->position++;
    }
}

static Value json_parse_value(JsonParser* parser);

static ObjString* json_parse_string(JsonParser* parser) {
    if (parser->text[parser->position] != '"') {
        parser->failed = true;
        return string_new_cstr(parser->vm, "");
    }
    parser->position++;

    size_t capacity = 16;
    size_t length = 0;
    char* buffer = (char*)malloc(capacity);
    if (!buffer) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }

    while (parser->text[parser->position] && parser->text[parser->position] != '"') {
        char c = parser->text[parser->position++];
        if (c == '\\') {
            char esc = parser->text[parser->position++];
            switch (esc) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case '/': c = '/'; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case 'u': {
                    char hex[5] = { 0 };
                    for (int i = 0; i < 4 && parser->text[parser->position]; i++) {
                        hex[i] = parser->text[parser->position++];
                    }
                    unsigned int code = (unsigned int)strtoul(hex, NULL, 16);
                    if (length + 4 >= capacity) {
                        capacity *= 2;
                        char* grown = (char*)realloc(buffer, capacity);
                        if (!grown) { free(buffer); fprintf(stderr, "sps: out of memory\n"); exit(1); }
                        buffer = grown;
                    }
                    if (code < 0x80) {
                        buffer[length++] = (char)code;
                    } else if (code < 0x800) {
                        buffer[length++] = (char)(0xC0 | (code >> 6));
                        buffer[length++] = (char)(0x80 | (code & 0x3F));
                    } else {
                        buffer[length++] = (char)(0xE0 | (code >> 12));
                        buffer[length++] = (char)(0x80 | ((code >> 6) & 0x3F));
                        buffer[length++] = (char)(0x80 | (code & 0x3F));
                    }
                    continue;
                }
                default: c = esc; break;
            }
        }
        if (length + 1 >= capacity) {
            capacity *= 2;
            char* grown = (char*)realloc(buffer, capacity);
            if (!grown) { free(buffer); fprintf(stderr, "sps: out of memory\n"); exit(1); }
            buffer = grown;
        }
        buffer[length++] = c;
    }

    if (parser->text[parser->position] == '"') parser->position++;
    else parser->failed = true;

    ObjString* result = string_new(parser->vm, buffer, length);
    free(buffer);
    return result;
}

static Value json_parse_value(JsonParser* parser) {
    json_skip_space(parser);
    char c = parser->text[parser->position];

    if (c == '{') {
        parser->position++;
        ObjDict* dict = dict_new(parser->vm);
        json_skip_space(parser);
        if (parser->text[parser->position] == '}') {
            parser->position++;
            return value_obj(&dict->obj);
        }
        for (;;) {
            json_skip_space(parser);
            ObjString* key = json_parse_string(parser);
            json_skip_space(parser);
            if (parser->text[parser->position] != ':') {
                parser->failed = true;
                break;
            }
            parser->position++;
            Value value = json_parse_value(parser);
            dict_set(parser->vm, dict, value_obj(&key->obj), value);
            json_skip_space(parser);
            if (parser->text[parser->position] == ',') {
                parser->position++;
                continue;
            }
            break;
        }
        json_skip_space(parser);
        if (parser->text[parser->position] == '}') parser->position++;
        else parser->failed = true;
        return value_obj(&dict->obj);
    }

    if (c == '[') {
        parser->position++;
        ObjList* list = list_new(parser->vm);
        json_skip_space(parser);
        if (parser->text[parser->position] == ']') {
            parser->position++;
            return value_obj(&list->obj);
        }
        for (;;) {
            Value value = json_parse_value(parser);
            list_push(parser->vm, list, value);
            json_skip_space(parser);
            if (parser->text[parser->position] == ',') {
                parser->position++;
                continue;
            }
            break;
        }
        json_skip_space(parser);
        if (parser->text[parser->position] == ']') parser->position++;
        else parser->failed = true;
        return value_obj(&list->obj);
    }

    if (c == '"') {
        ObjString* str = json_parse_string(parser);
        return value_obj(&str->obj);
    }

    if (strncmp(parser->text + parser->position, "true", 4) == 0) {
        parser->position += 4;
        return value_bool(true);
    }
    if (strncmp(parser->text + parser->position, "false", 5) == 0) {
        parser->position += 5;
        return value_bool(false);
    }
    if (strncmp(parser->text + parser->position, "null", 4) == 0) {
        parser->position += 4;
        return value_nil();
    }

    if (c == '-' || isdigit((unsigned char)c)) {
        char* end = NULL;
        double number = strtod(parser->text + parser->position, &end);
        if (end == parser->text + parser->position) {
            parser->failed = true;
            return value_nil();
        }
        bool is_integer = true;
        for (const char* p = parser->text + parser->position; p < end; p++) {
            if (*p == '.' || *p == 'e' || *p == 'E') is_integer = false;
        }
        parser->position = (size_t)(end - parser->text);
        return is_integer ? value_int((int64_t)number) : value_float(number);
    }

    parser->failed = true;
    return value_nil();
}

// Parses a json text directly (the host uses it for the project file).
Value sps_json_parse_text(VM* vm, const char* text, size_t length, bool* ok) {
    (void)length;
    JsonParser parser;
    parser.text = text;
    parser.position = 0;
    parser.vm = vm;
    parser.failed = false;

    Value result = json_parse_value(&parser);
    if (parser.failed) {
        if (ok) *ok = false;
        return value_nil();
    }
    if (ok) *ok = true;
    return result;
}

Value sps_json_parse(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "json_parse() expects 1 argument");
    const char* text = arg_string(vm, args[0], "json_parse");

    JsonParser parser;
    parser.text = text;
    parser.position = 0;
    parser.vm = vm;
    parser.failed = false;

    Value result = json_parse_value(&parser);
    if (parser.failed) {
        vm_runtime_error(vm, 0, "invalid JSON");
    }
    return result;
}

// a growable text buffer, used by the json writer
typedef struct {
    char*  data;
    size_t length;
    size_t capacity;
} TextBuffer;

static void buffer_init(TextBuffer* buffer) {
    buffer->capacity = 128;
    buffer->length = 0;
    buffer->data = (char*)malloc(buffer->capacity);
    if (!buffer->data) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    buffer->data[0] = '\0';
}

static void buffer_append(TextBuffer* buffer, const char* text, size_t length) {
    if (buffer->length + length + 1 > buffer->capacity) {
        while (buffer->length + length + 1 > buffer->capacity) buffer->capacity *= 2;
        char* grown = (char*)realloc(buffer->data, buffer->capacity);
        if (!grown) {
            free(buffer->data);
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        buffer->data = grown;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
}

static void buffer_puts(TextBuffer* buffer, const char* text) {
    buffer_append(buffer, text, strlen(text));
}

static void buffer_putc(TextBuffer* buffer, char c) {
    buffer_append(buffer, &c, 1);
}

static void json_write_string(TextBuffer* out, const char* text, size_t length) {
    buffer_putc(out, '"');
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        switch (c) {
            case '"':  buffer_puts(out, "\\\""); break;
            case '\\': buffer_puts(out, "\\\\"); break;
            case '\n': buffer_puts(out, "\\n"); break;
            case '\t': buffer_puts(out, "\\t"); break;
            case '\r': buffer_puts(out, "\\r"); break;
            default:   buffer_putc(out, c); break;
        }
    }
    buffer_putc(out, '"');
}

static void json_write_value(TextBuffer* out, Value value, bool pretty, int level) {
    switch (value.type) {
        case VAL_NIL:
            buffer_puts(out, "null");
            break;
        case VAL_BOOL:
            buffer_puts(out, value.as.boolean ? "true" : "false");
            break;
        case VAL_INT: {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%lld", (long long)value.as.integer);
            buffer_puts(out, buffer);
            break;
        }
        case VAL_FLOAT:
        case VAL_FLOAT32: {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "%.17g", value.as.number);
            buffer_puts(out, buffer);
            break;
        }
        case VAL_STRING: {
            ObjString* str = (ObjString*)value.as.obj;
            json_write_string(out, str->chars, str->len);
            break;
        }
        case VAL_LIST: {
            ObjList* list = (ObjList*)value.as.obj;
            buffer_putc(out, '[');
            for (uint32_t i = 0; i < list->count; i++) {
                if (i) buffer_putc(out, ',');
                if (pretty) {
                    buffer_putc(out, '\n');
                    for (int s = 0; s <= level; s++) buffer_puts(out, "  ");
                }
                json_write_value(out, list->items[i], pretty, level + 1);
            }
            if (pretty && list->count) {
                buffer_putc(out, '\n');
                for (int s = 0; s < level; s++) buffer_puts(out, "  ");
            }
            buffer_putc(out, ']');
            break;
        }
        case VAL_DICT: {
            ObjDict* dict = (ObjDict*)value.as.obj;
            buffer_putc(out, '{');
            bool first = true;
            for (uint32_t i = 0; i < dict->capacity; i++) {
                if (dict->entries[i].state != 1) continue;
                if (!first) buffer_putc(out, ',');
                first = false;
                if (pretty) {
                    buffer_putc(out, '\n');
                    for (int s = 0; s <= level; s++) buffer_puts(out, "  ");
                }
                json_write_value(out, dict->entries[i].key, pretty, level + 1);
                buffer_puts(out, pretty ? ": " : ":");
                json_write_value(out, dict->entries[i].value, pretty, level + 1);
            }
            if (pretty && !first) {
                buffer_putc(out, '\n');
                for (int s = 0; s < level; s++) buffer_puts(out, "  ");
            }
            buffer_putc(out, '}');
            break;
        }
        default:
            buffer_puts(out, "null");
            break;
    }
}

Value sps_json_stringify(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "json_stringify() expects 1 argument");

    TextBuffer buffer;
    buffer_init(&buffer);
    json_write_value(&buffer, args[0], false, 0);

    ObjString* result = string_new(vm, buffer.data, buffer.length);
    free(buffer.data);
    return value_obj(&result->obj);
}

// ------------------------------------------------------------------
// formatting
// ------------------------------------------------------------------

// Formats a value with a format spec, for example ">10.2f".
ObjString* sps_format_value(VM* vm, Value value, const char* spec, int spec_length) {
    if (spec_length <= 0) return string_from_value(vm, value);

    int index = 0;
    bool left_align = false;
    if (index < spec_length && (spec[index] == '<' || spec[index] == '>')) {
        left_align = spec[index] == '<';
        index++;
    }

    int width = 0;
    while (index < spec_length && spec[index] >= '0' && spec[index] <= '9') {
        width = width * 10 + (spec[index] - '0');
        index++;
    }

    int precision = -1;
    if (index < spec_length && spec[index] == '.') {
        index++;
        precision = 0;
        while (index < spec_length && spec[index] >= '0' && spec[index] <= '9') {
            precision = precision * 10 + (spec[index] - '0');
            index++;
        }
    }

    char type = (index < spec_length) ? spec[index] : '\0';
    char buffer[512];
    size_t length = 0;

    switch (type) {
        case 'f':
        case 'F': {
            double number = value.type == VAL_INT ? (double)value.as.integer : value.as.number;
            snprintf(buffer, sizeof(buffer), "%.*f", precision >= 0 ? precision : 6, number);
            length = strlen(buffer);
            break;
        }
        case 'e':
        case 'E': {
            double number = value.type == VAL_INT ? (double)value.as.integer : value.as.number;
            snprintf(buffer, sizeof(buffer), "%.*e", precision >= 0 ? precision : 6, number);
            length = strlen(buffer);
            break;
        }
        case 'd': {
            int64_t number = value.type == VAL_INT ? value.as.integer : (int64_t)value.as.number;
            snprintf(buffer, sizeof(buffer), "%lld", (long long)number);
            length = strlen(buffer);
            break;
        }
        case 'x':
        case 'X': {
            int64_t number = value.type == VAL_INT ? value.as.integer : (int64_t)value.as.number;
            snprintf(buffer, sizeof(buffer), type == 'x' ? "%llx" : "%llX",
                     (unsigned long long)number);
            length = strlen(buffer);
            break;
        }
        case 's': {
            ObjString* text = string_from_value(vm, value);
            if (precision >= 0 && precision < (int)text->len) {
                ObjString* cut = string_new(vm, text->chars, (size_t)precision);
                text = cut;
            }
            length = text->len;
            if (length >= sizeof(buffer)) length = sizeof(buffer) - 1;
            memcpy(buffer, text->chars, length);
            break;
        }
        default: {
            if (value_is_number(value) && precision >= 0) {
                double number = value.type == VAL_INT ? (double)value.as.integer : value.as.number;
                snprintf(buffer, sizeof(buffer), "%.*f", precision, number);
                length = strlen(buffer);
            } else {
                ObjString* text = string_from_value(vm, value);
                length = text->len;
                if (length >= sizeof(buffer)) length = sizeof(buffer) - 1;
                memcpy(buffer, text->chars, length);
            }
            break;
        }
    }

    if ((int)length < width) {
        size_t padding = (size_t)width - length;
        size_t total = length + padding;
        char* result = (char*)malloc(total + 1);
        if (!result) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        if (left_align) {
            memcpy(result, buffer, length);
            memset(result + length, ' ', padding);
        } else {
            memset(result, ' ', padding);
            memcpy(result + padding, buffer, length);
        }
        result[total] = '\0';
        ObjString* str = string_new(vm, result, total);
        free(result);
        return str;
    }

    return string_new(vm, buffer, length);
}

// Replaces the fields of a template: {}, {0}, {name} and the format specs.
ObjString* sps_format_template(VM* vm, const char* text, int length, Value* args, int argc) {
    size_t capacity = (size_t)length + 64;
    char* buffer = (char*)malloc(capacity);
    if (!buffer) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    size_t out = 0;
    int position = 0;
    int next_argument = 0;

    while (position < length) {
        char c = text[position];
        if (c == '{' && position + 1 < length && text[position + 1] == '{') {
            if (out + 2 >= capacity) {
                capacity *= 2;
                buffer = (char*)realloc(buffer, capacity);
                if (!buffer) { fprintf(stderr, "sps: out of memory\n"); exit(1); }
            }
            buffer[out++] = '{';
            position += 2;
            continue;
        }
        if (c == '}' && position + 1 < length && text[position + 1] == '}') {
            if (out + 2 >= capacity) {
                capacity *= 2;
                buffer = (char*)realloc(buffer, capacity);
                if (!buffer) { fprintf(stderr, "sps: out of memory\n"); exit(1); }
            }
            buffer[out++] = '}';
            position += 2;
            continue;
        }
        if (c != '{') {
            if (out + 2 >= capacity) {
                capacity *= 2;
                buffer = (char*)realloc(buffer, capacity);
                if (!buffer) { fprintf(stderr, "sps: out of memory\n"); exit(1); }
            }
            buffer[out++] = c;
            position++;
            continue;
        }

        // the field: the name, the optional spec and the closing "}"
        int field_start = position + 1;
        int scan = field_start;
        int depth = 1;
        int spec_start = -1;
        while (scan < length && depth > 0) {
            if (text[scan] == '{') depth++;
            else if (text[scan] == '}') {
                depth--;
                if (depth == 0) break;
            } else if (text[scan] == ':' && depth == 1 && spec_start < 0) {
                spec_start = scan;
            }
            scan++;
        }
        if (scan >= length) {
            free(buffer);
            vm_runtime_error(vm, 0, "unterminated field in the format string");
        }

        int name_end = (spec_start >= 0) ? spec_start : scan;
        int name_length = name_end - field_start;

        Value value;
        if (name_length == 0) {
            if (next_argument >= argc) {
                free(buffer);
                vm_runtime_error(vm, 0, "not enough arguments for the format string");
            }
            value = args[next_argument++];
        } else if (name_length > 0 && name_length <= 9 &&
                   text[field_start] >= '0' && text[field_start] <= '9') {
            int index = 0;
            for (int i = 0; i < name_length; i++) {
                index = index * 10 + (text[field_start + i] - '0');
            }
            if (index >= argc) {
                free(buffer);
                vm_runtime_error(vm, 0, "not enough arguments for the format string");
            }
            value = args[index];
        } else {
            // a named field: the single argument must be a dictionary
            if (argc == 1 && args[0].type == VAL_DICT) {
                ObjDict* dict = (ObjDict*)args[0].as.obj;
                ObjString* key = string_new(vm, text + field_start, (size_t)name_length);
                if (!dict_get(dict, value_obj(&key->obj), &value)) {
                    free(buffer);
                    vm_runtime_error(vm, 0, "the format string has no field '%s'", key->chars);
                }
            } else {
                free(buffer);
                vm_runtime_error(vm, 0, "unknown field in the format string");
            }
        }

        const char* spec = "";
        int spec_length = 0;
        if (spec_start >= 0) {
            spec = text + spec_start + 1;
            spec_length = scan - spec_start - 1;
        }

        ObjString* piece = sps_format_value(vm, value, spec, spec_length);
        size_t needed = out + piece->len + 2;
        if (needed >= capacity) {
            while (needed >= capacity) capacity *= 2;
            buffer = (char*)realloc(buffer, capacity);
            if (!buffer) { fprintf(stderr, "sps: out of memory\n"); exit(1); }
        }
        memcpy(buffer + out, piece->chars, piece->len);
        out += piece->len;

        position = scan + 1;
    }

    buffer[out] = '\0';
    ObjString* result = string_new(vm, buffer, out);
    free(buffer);
    return result;
}

Value sps_builtin_format(VM* vm, int argc, Value* args, void* user) {
    if (argc < 1) vm_runtime_error(vm, 0, "format() expects at least 1 argument");
    ObjString* text = (ObjString*)args[0].as.obj;
    ObjString* result = sps_format_template(vm, text->chars, text->len, args + 1, argc - 1);
    return value_obj(&result->obj);
}

Value sps_method_format(VM* vm, int argc, Value* args, void* user) {
    ObjString* text = (ObjString*)args[0].as.obj;
    ObjString* result = sps_format_template(vm, text->chars, text->len, args + 1, argc - 1);
    return value_obj(&result->obj);
}

// ------------------------------------------------------------------
// the collections, the formats and the assertions
// ------------------------------------------------------------------

static Value list_of(VM* vm, Value value) {
    if (value.type != VAL_LIST) {
        vm_runtime_error(vm, 0, "a list is expected");
    }
    return value;
}

// zip(a, b) - the pairs of the two lists
Value sps_zip(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 2) vm_runtime_error(vm, 0, "zip() expects 2 arguments");
    ObjList* left = (ObjList*)list_of(vm, args[0]).as.obj;
    ObjList* right = (ObjList*)list_of(vm, args[1]).as.obj;

    ObjList* result = list_new(vm);
    uint32_t count = left->count < right->count ? left->count : right->count;
    for (uint32_t i = 0; i < count; i++) {
        ObjList* pair = list_new(vm);
        list_push(vm, pair, left->items[i]);
        list_push(vm, pair, right->items[i]);
        list_push(vm, result, value_obj(&pair->obj));
    }
    return value_obj(&result->obj);
}

// unique(list) - the items without the repeats
Value sps_unique(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 1) vm_runtime_error(vm, 0, "unique() expects 1 argument");
    ObjList* source = (ObjList*)list_of(vm, args[0]).as.obj;

    ObjList* result = list_new(vm);
    for (uint32_t i = 0; i < source->count; i++) {
        bool seen = false;
        for (uint32_t j = 0; j < result->count; j++) {
            if (values_equal(result->items[j], source->items[i])) { seen = true; break; }
        }
        if (!seen) list_push(vm, result, source->items[i]);
    }
    return value_obj(&result->obj);
}

// flatten(list) - the lists inside the list become one list
Value sps_flatten(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 1) vm_runtime_error(vm, 0, "flatten() expects 1 argument");
    ObjList* source = (ObjList*)list_of(vm, args[0]).as.obj;

    ObjList* result = list_new(vm);
    for (uint32_t i = 0; i < source->count; i++) {
        if (source->items[i].type == VAL_LIST) {
            ObjList* inner = (ObjList*)source->items[i].as.obj;
            for (uint32_t j = 0; j < inner->count; j++) {
                list_push(vm, result, inner->items[j]);
            }
            continue;
        }
        list_push(vm, result, source->items[i]);
    }
    return value_obj(&result->obj);
}

// sortBy(list, function) - the list is sorted by the result of the function
Value sps_sort_by(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 2) vm_runtime_error(vm, 0, "sortBy() expects 2 arguments");
    ObjList* source = (ObjList*)list_of(vm, args[0]).as.obj;

    // the keys are computed once
    uint32_t count = source->count;
    Value* items = NULL;
    Value* keys = NULL;
    if (count) {
        items = (Value*)malloc(sizeof(Value) * count);
        keys = (Value*)malloc(sizeof(Value) * count);
        if (!items || !keys) {
            free(items);
            free(keys);
            vm_runtime_error(vm, 0, "out of memory");
        }
        for (uint32_t i = 0; i < count; i++) {
            items[i] = source->items[i];
            Value out;
            if (!vm_call_value(vm, args[1], 1, &items[i], &out)) {
                free(items);
                free(keys);
                vm_runtime_error(vm, 0, "the function of sortBy() failed");
            }
            keys[i] = out;
        }
    }

    // the insertion sort by the keys
    for (uint32_t i = 1; i < count; i++) {
        Value item = items[i];
        Value key = keys[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0) {
            int cmp = 0;
            if (!values_compare(keys[j], key, &cmp)) break;
            if (cmp <= 0) break;
            items[j + 1] = items[j];
            keys[j + 1] = keys[j];
            j--;
        }
        items[j + 1] = item;
        keys[j + 1] = key;
    }

    ObjList* result = list_new(vm);
    for (uint32_t i = 0; i < count; i++) list_push(vm, result, items[i]);
    free(items);
    free(keys);
    return value_obj(&result->obj);
}

// merge(a, b) - the dictionaries are merged into a new one
Value sps_merge(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 2) vm_runtime_error(vm, 0, "merge() expects 2 arguments");
    if (args[0].type != VAL_DICT || args[1].type != VAL_DICT) {
        vm_runtime_error(vm, 0, "merge() expects two dictionaries");
    }
    ObjDict* left = (ObjDict*)args[0].as.obj;
    ObjDict* right = (ObjDict*)args[1].as.obj;
    ObjDict* result = dict_new(vm);

    for (uint32_t i = 0; i < left->capacity; i++) {
        if (left->entries[i].state == 1) {
            dict_set(vm, result, left->entries[i].key, left->entries[i].value);
        }
    }
    for (uint32_t i = 0; i < right->capacity; i++) {
        if (right->entries[i].state == 1) {
            dict_set(vm, result, right->entries[i].key, right->entries[i].value);
        }
    }
    return value_obj(&result->obj);
}

// deepCopy(value) - the copy of the lists and the dictionaries
static Value copy_value(VM* vm, Value value) {
    if (value.type == VAL_LIST) {
        ObjList* source = (ObjList*)value.as.obj;
        ObjList* result = list_new(vm);
        for (uint32_t i = 0; i < source->count; i++) {
            list_push(vm, result, copy_value(vm, source->items[i]));
        }
        return value_obj(&result->obj);
    }
    if (value.type == VAL_DICT) {
        ObjDict* source = (ObjDict*)value.as.obj;
        ObjDict* result = dict_new(vm);
        for (uint32_t i = 0; i < source->capacity; i++) {
            if (source->entries[i].state == 1) {
                dict_set(vm, result, source->entries[i].key, copy_value(vm, source->entries[i].value));
            }
        }
        return value_obj(&result->obj);
    }
    return value;
}

Value sps_deep_copy(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 1) vm_runtime_error(vm, 0, "deepCopy() expects 1 argument");
    return copy_value(vm, args[0]);
}

// first(list), last(list), take(list, n), skip(list, n), indexOf(list, value)
Value sps_first(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 1) vm_runtime_error(vm, 0, "first() expects 1 argument");
    ObjList* source = (ObjList*)list_of(vm, args[0]).as.obj;
    if (source->count == 0) return value_nil();
    return source->items[0];
}

Value sps_last(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 1) vm_runtime_error(vm, 0, "last() expects 1 argument");
    ObjList* source = (ObjList*)list_of(vm, args[0]).as.obj;
    if (source->count == 0) return value_nil();
    return source->items[source->count - 1];
}

Value sps_take(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 2) vm_runtime_error(vm, 0, "take() expects 2 arguments");
    ObjList* source = (ObjList*)list_of(vm, args[0]).as.obj;
    int64_t count = arg_int(vm, args[1], "x");
    ObjList* result = list_new(vm);
    for (int64_t i = 0; i < count && (uint32_t)i < source->count; i++) {
        list_push(vm, result, source->items[i]);
    }
    return value_obj(&result->obj);
}

Value sps_skip(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 2) vm_runtime_error(vm, 0, "skip() expects 2 arguments");
    ObjList* source = (ObjList*)list_of(vm, args[0]).as.obj;
    int64_t count = arg_int(vm, args[1], "x");
    ObjList* result = list_new(vm);
    for (uint32_t i = 0; i < source->count; i++) {
        if ((int64_t)i >= count) list_push(vm, result, source->items[i]);
    }
    return value_obj(&result->obj);
}

Value sps_index_of(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 2) vm_runtime_error(vm, 0, "indexOf() expects 2 arguments");
    ObjList* source = (ObjList*)list_of(vm, args[0]).as.obj;
    for (uint32_t i = 0; i < source->count; i++) {
        if (values_equal(source->items[i], args[1])) return value_int((int64_t)i);
    }
    return value_int(-1);
}

// The formats of the numbers and the strings
Value sps_to_hex(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 1) vm_runtime_error(vm, 0, "toHex() expects 1 argument");
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%llx", (unsigned long long)arg_int(vm, args[0], "x"));
    ObjString* result = string_new_cstr(vm, buffer);
    return value_obj(&result->obj);
}

Value sps_to_binary(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 1) vm_runtime_error(vm, 0, "toBinary() expects 1 argument");
    uint64_t number = (uint64_t)arg_int(vm, args[0], "x");
    char buffer[72];
    int position = 71;
    buffer[position] = '\0';
    if (number == 0) {
        buffer[--position] = '0';
    } else {
        while (number > 0 && position > 0) {
            buffer[--position] = (char)('0' + (number & 1));
            number >>= 1;
        }
    }
    ObjString* result = string_new_cstr(vm, buffer + position);
    return value_obj(&result->obj);
}

Value sps_to_fixed(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 2) vm_runtime_error(vm, 0, "toFixed() expects 2 arguments");
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.*f", (int)arg_int(vm, args[1], "x"),
             arg_double(vm, args[0], "x"));
    ObjString* result = string_new_cstr(vm, buffer);
    return value_obj(&result->obj);
}

Value sps_pad_start(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc < 2) vm_runtime_error(vm, 0, "padStart() expects 2 arguments");
    const char* text = arg_string(vm, args[0], "padStart");
    size_t length = strlen(text);
    int64_t width = arg_int(vm, args[1], "x");
    char fill = argc > 2 && strlen(arg_string(vm, args[2], "padStart")) > 0
                    ? arg_string(vm, args[2], "padStart")[0]
                    : ' ';
    if ((int64_t)length >= width) {
        ObjString* result = string_new_cstr(vm, text);
        return value_obj(&result->obj);
    }
    char* buffer = (char*)malloc((size_t)width + 1);
    if (!buffer) vm_runtime_error(vm, 0, "out of memory");
    for (int64_t i = 0; i < width - (int64_t)length; i++) buffer[i] = fill;
    memcpy(buffer + (width - (int64_t)length), text, length);
    buffer[width] = '\0';
    ObjString* result = string_new_cstr(vm, buffer);
    free(buffer);
    return value_obj(&result->obj);
}

Value sps_pad_end(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc < 2) vm_runtime_error(vm, 0, "padEnd() expects 2 arguments");
    const char* text = arg_string(vm, args[0], "padEnd");
    size_t length = strlen(text);
    int64_t width = arg_int(vm, args[1], "x");
    char fill = argc > 2 && strlen(arg_string(vm, args[2], "padEnd")) > 0
                    ? arg_string(vm, args[2], "padEnd")[0]
                    : ' ';
    if ((int64_t)length >= width) {
        ObjString* result = string_new_cstr(vm, text);
        return value_obj(&result->obj);
    }
    char* buffer = (char*)malloc((size_t)width + 1);
    if (!buffer) vm_runtime_error(vm, 0, "out of memory");
    memcpy(buffer, text, length);
    for (int64_t i = (int64_t)length; i < width; i++) buffer[i] = fill;
    buffer[width] = '\0';
    ObjString* result = string_new_cstr(vm, buffer);
    free(buffer);
    return value_obj(&result->obj);
}

Value sps_char_code(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc < 1) vm_runtime_error(vm, 0, "charCode() expects 1 argument");
    const char* text = arg_string(vm, args[0], "charCode");
    int64_t index = argc > 1 ? arg_int(vm, args[1], "x") : 0;
    if (index < 0 || (size_t)index >= strlen(text)) return value_int(-1);
    return value_int((unsigned char)text[index]);
}

Value sps_from_char_code(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 1) vm_runtime_error(vm, 0, "fromCharCode() expects 1 argument");
    char c = (char)arg_int(vm, args[0], "x");
    ObjString* result = string_new(vm, &c, 1);
    return value_obj(&result->obj);
}

// The assertions of the tests
Value sps_assert(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc < 1) vm_runtime_error(vm, 0, "assert() expects 1 argument");
    if (!value_is_truthy(args[0])) {
        const char* message = argc > 1 ? arg_string(vm, args[1], "assert") : "the assertion is false";
        vm_runtime_error(vm, 0, "assert: %s", message);
    }
    return value_nil();
}

Value sps_assert_equal(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc < 2) vm_runtime_error(vm, 0, "assertEqual() expects 2 arguments");
    if (!values_equal(args[0], args[1])) {
        const char* message = argc > 2 ? arg_string(vm, args[2], "assertEqual") : "the values are different";
        vm_runtime_error(vm, 0, "assertEqual: %s", message);
    }
    return value_nil();
}

// ------------------------------------------------------------------
// the Result
// ------------------------------------------------------------------
//
// A Result is a dictionary with the tag "ok" and either the value or the
// message of the error. It is the way to report an error without an
// exception: the caller has to look at it.

static Value make_result(VM* vm, bool ok, Value value, bool has_value) {
    ObjList* list = list_new(vm);
    (void)list;

    ObjDict* dict = dict_new(vm);
    ObjString* tag = string_new_cstr(vm, "ok");
    dict_set(vm, dict, value_obj(&tag->obj), value_bool(ok));

    ObjString* key = string_new_cstr(vm, has_value ? "value" : "error");
    dict_set(vm, dict, value_obj(&key->obj), value);
    return value_obj(&dict->obj);
}

Value sps_result_ok(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    Value value = argc > 0 ? args[0] : value_nil();
    return make_result(vm, true, value, true);
}

Value sps_result_error(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    Value message = argc > 0 ? args[0] : value_nil();
    return make_result(vm, false, message, false);
}

static bool result_is_ok(VM* vm, Value result) {
    if (result.type != VAL_DICT) return false;
    ObjDict* dict = (ObjDict*)result.as.obj;
    Value tag;
    if (!dict_get(dict, value_obj(&string_new_cstr(vm, "ok")->obj), &tag)) return false;
    return value_is_truthy(tag);
}

Value sps_is_ok(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc < 1) return value_bool(false);
    return value_bool(result_is_ok(vm, args[0]));
}

Value sps_is_error(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc < 1) return value_bool(true);
    return value_bool(!result_is_ok(vm, args[0]));
}

Value sps_unwrap(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc < 1) return value_nil();
    if (!result_is_ok(vm, args[0])) {
        vm_runtime_error(vm, 0, "unwrap() of an error");
    }
    ObjDict* dict = (ObjDict*)args[0].as.obj;
    Value value;
    if (!dict_get(dict, value_obj(&string_new_cstr(vm, "value")->obj), &value)) return value_nil();
    return value;
}

Value sps_unwrap_or(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc < 1) return value_nil();
    if (!result_is_ok(vm, args[0])) {
        return argc > 1 ? args[1] : value_nil();
    }
    ObjDict* dict = (ObjDict*)args[0].as.obj;
    Value value;
    if (!dict_get(dict, value_obj(&string_new_cstr(vm, "value")->obj), &value)) return value_nil();
    return value;
}

// ------------------------------------------------------------------
// the SPL format
// ------------------------------------------------------------------

Value spl_builtin_parse(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 1) vm_runtime_error(vm, 0, "spl_parse() expects 1 argument");
    const char* text = arg_string(vm, args[0], "spl_parse");

    bool ok = false;
    Value result = spl_parse(vm, text, strlen(text), &ok);
    if (!ok) vm_runtime_error(vm, 0, "the text is not a valid SPL document");
    return result;
}

Value spl_builtin_stringify(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 1) vm_runtime_error(vm, 0, "spl_stringify() expects 1 argument");
    ObjString* text = spl_stringify(vm, args[0], false, 0);
    return value_obj(&text->obj);
}

Value spl_builtin_pretty(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 1) vm_runtime_error(vm, 0, "spl_pretty() expects 1 argument");
    ObjString* text = spl_stringify(vm, args[0], true, 2);
    return value_obj(&text->obj);
}

Value spl_builtin_load(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 1) vm_runtime_error(vm, 0, "spl_load() expects 1 argument");
    const char* path = arg_string(vm, args[0], "spl_load");

    FILE* file = fopen(path, "rb");
    if (!file) vm_runtime_error(vm, 0, "cannot open '%s'", path);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) size = 0;
    char* text = (char*)malloc((size_t)size + 1);
    if (!text) {
        fclose(file);
        vm_runtime_error(vm, 0, "out of memory");
    }
    size_t read = fread(text, 1, (size_t)size, file);
    fclose(file);
    text[read] = '\0';

    bool ok = false;
    Value result = spl_parse(vm, text, read, &ok);
    free(text);
    if (!ok) vm_runtime_error(vm, 0, "'%s' is not a valid SPL document", path);
    return result;
}

Value spl_builtin_save(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 2) vm_runtime_error(vm, 0, "spl_save() expects 2 arguments");
    const char* path = arg_string(vm, args[0], "spl_save");
    ObjString* text = spl_stringify(vm, args[1], false, 0);

    FILE* file = fopen(path, "wb");
    if (!file) return value_bool(false);
    fwrite(text->chars, 1, text->len, file);
    fclose(file);
    return value_bool(true);
}

Value sps_json_pretty(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 1) vm_runtime_error(vm, 0, "json_pretty() expects 1 argument");

    TextBuffer buffer;
    buffer_init(&buffer);
    json_write_value(&buffer, args[0], true, 0);

    ObjString* result = string_new(vm, buffer.data, buffer.length);
    free(buffer.data);
    return value_obj(&result->obj);
}

// ------------------------------------------------------------------
// the functions of the collections
// ------------------------------------------------------------------

// map(list, function) - a new list with the results of the function
Value sps_map(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 2) vm_runtime_error(vm, 0, "map() expects 2 arguments");
    if (args[0].type != VAL_LIST) vm_runtime_error(vm, 0, "map() expects a list");

    ObjList* source = (ObjList*)args[0].as.obj;
    ObjList* result = list_new(vm);
    for (uint32_t i = 0; i < source->count; i++) {
        Value item = source->items[i];
        Value out;
        if (!vm_call_value(vm, args[1], 1, &item, &out)) {
            vm_runtime_error(vm, 0, "the function of map() failed");
        }
        list_push(vm, result, out);
    }
    return value_obj(&result->obj);
}

// filter(list, function) - the items the function accepts
Value sps_filter(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 2) vm_runtime_error(vm, 0, "filter() expects 2 arguments");
    if (args[0].type != VAL_LIST) vm_runtime_error(vm, 0, "filter() expects a list");

    ObjList* source = (ObjList*)args[0].as.obj;
    ObjList* result = list_new(vm);
    for (uint32_t i = 0; i < source->count; i++) {
        Value item = source->items[i];
        Value out;
        if (!vm_call_value(vm, args[1], 1, &item, &out)) {
            vm_runtime_error(vm, 0, "the function of filter() failed");
        }
        if (value_is_truthy(out)) list_push(vm, result, item);
    }
    return value_obj(&result->obj);
}

// reduce(list, function, start) - the folded value of the list
Value sps_reduce(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc < 2 || argc > 3) vm_runtime_error(vm, 0, "reduce() expects 2 or 3 arguments");
    if (args[0].type != VAL_LIST) vm_runtime_error(vm, 0, "reduce() expects a list");

    ObjList* source = (ObjList*)args[0].as.obj;
    Value accumulator = (argc > 2) ? args[2] : value_nil();
    bool first = (argc <= 2);

    for (uint32_t i = 0; i < source->count; i++) {
        if (first) {
            accumulator = source->items[i];
            first = false;
            continue;
        }
        Value arguments[2];
        arguments[0] = accumulator;
        arguments[1] = source->items[i];
        Value out;
        if (!vm_call_value(vm, args[1], 2, arguments, &out)) {
            vm_runtime_error(vm, 0, "the function of reduce() failed");
        }
        accumulator = out;
    }
    return accumulator;
}

// ------------------------------------------------------------------
// math
// ------------------------------------------------------------------

Value sps_sqrt(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "sqrt() expects 1 argument");
    return value_float(sqrt(arg_number(vm, args[0], "sqrt")));
}

Value sps_sin(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "sin() expects 1 argument");
    return value_float(sin(arg_number(vm, args[0], "sin")));
}

Value sps_cos(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "cos() expects 1 argument");
    return value_float(cos(arg_number(vm, args[0], "cos")));
}

Value sps_tan(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "tan() expects 1 argument");
    return value_float(tan(arg_number(vm, args[0], "tan")));
}

Value sps_atan2(VM* vm, int argc, Value* args, void* user) {
    if (argc != 2) vm_runtime_error(vm, 0, "atan2() expects 2 arguments");
    double y = arg_number(vm, args[0], "atan2");
    double x = arg_number(vm, args[1], "atan2");
    return value_float(atan2(y, x));
}

Value sps_pow(VM* vm, int argc, Value* args, void* user) {
    if (argc != 2) vm_runtime_error(vm, 0, "pow() expects 2 arguments");
    double base = arg_number(vm, args[0], "pow");
    double exponent = arg_number(vm, args[1], "pow");
    return value_float(pow(base, exponent));
}

Value sps_floor(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "floor() expects 1 argument");
    return value_float(floor(arg_number(vm, args[0], "floor")));
}

Value sps_ceil(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "ceil() expects 1 argument");
    return value_float(ceil(arg_number(vm, args[0], "ceil")));
}

Value sps_round(VM* vm, int argc, Value* args, void* user) {
    if (argc != 1) vm_runtime_error(vm, 0, "round() expects 1 argument");
    return value_float(round(arg_number(vm, args[0], "round")));
}

static uint32_t random_state = 2463534242u;

static uint32_t random_next(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

Value sps_random(VM* vm, int argc, Value* args, void* user) {
    (void)vm;
    (void)argc;
    (void)args;
    return value_float((double)random_next() / 4294967296.0);
}

Value sps_random_range(VM* vm, int argc, Value* args, void* user) {
    if (argc != 2) vm_runtime_error(vm, 0, "random_range() expects 2 arguments");
    int64_t low = (int64_t)arg_number(vm, args[0], "random_range");
    int64_t high = (int64_t)arg_number(vm, args[1], "random_range");
    if (high <= low) return value_int(low);
    int64_t span = high - low + 1;
    return value_int(low + (int64_t)(random_next() % (uint32_t)span));
}

// ------------------------------------------------------------------
// the regular expressions
// ------------------------------------------------------------------
//
// A regular expression is an object of the virtual machine: it keeps the
// compiled pattern and the machine releases it with the other objects.

static void regex_object_free(void* data) {
    regex_free((Regex*)data);
}

// The class of the objects of the regular expressions. It is registered in
// the virtual machine, so the machine releases it at the shutdown.
static HostClass* regex_class(VM* vm) {
    for (int i = 0; i < vm->host_class_count; i++) {
        HostClass* klass = vm->host_classes[i];
        if (strcmp(klass->name, "Regex") == 0) return klass;
    }
    int index = vm_register_host_class(vm, "Regex");
    if (index < 0) return NULL;
    HostClass* klass = vm->host_classes[index];
    klass->free_fn = regex_object_free;
    return klass;
}

static Regex* arg_regex(VM* vm, Value value, const char* function) {
    if (value.type != VAL_USERDATA) {
        vm_runtime_error(vm, 0, "'%s' expects a regular expression", function);
    }
    ObjUserdata* object = (ObjUserdata*)value.as.obj;
    if (!object->klass || strcmp(object->klass->name, "Regex") != 0) {
        vm_runtime_error(vm, 0, "'%s' expects a regular expression", function);
    }
    return (Regex*)object->data;
}

static ObjString* arg_text(VM* vm, Value value, const char* function) {
    if (value.type != VAL_STRING) {
        vm_runtime_error(vm, 0, "'%s' expects a string", function);
    }
    return (ObjString*)value.as.obj;
}

// regex(pattern) - the compiled pattern
Value sps_regex_new(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 1) vm_runtime_error(vm, 0, "regex() expects 1 argument");
    const char* pattern = arg_string(vm, args[0], "regex");

    char error[160];
    Regex* regex = regex_compile(pattern, error, sizeof(error));
    if (!regex) {
        vm_runtime_error(vm, 0, "invalid regular expression: %s", error);
        return value_nil();
    }

    HostClass* klass = regex_class(vm);
    if (!klass) {
        regex_free(regex);
        vm_runtime_error(vm, 0, "out of memory");
        return value_nil();
    }

    ObjUserdata* object = userdata_new(vm, klass, regex);
    return value_obj(&object->obj);
}

// regexMatch(re, text) - the whole text matches the pattern
Value sps_regex_match(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 2) vm_runtime_error(vm, 0, "regexMatch() expects 2 arguments");
    Regex* regex = arg_regex(vm, args[0], "regexMatch");
    ObjString* text = arg_text(vm, args[1], "regexMatch");

    size_t begin = 0;
    size_t end = 0;
    return value_bool(regex_match(regex, text->chars, text->len, &begin, &end));
}

// regexTest(re, text) - the pattern is found somewhere in the text
Value sps_regex_test(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 2) vm_runtime_error(vm, 0, "regexTest() expects 2 arguments");
    Regex* regex = arg_regex(vm, args[0], "regexTest");
    ObjString* text = arg_text(vm, args[1], "regexTest");

    size_t begin = 0;
    size_t end = 0;
    return value_bool(regex_search(regex, text->chars, text->len, 0, &begin, &end,
                                    NULL, NULL));
}

// regexSearch(re, text) - the list of the matches: every match is a list
// with the whole match and the groups
Value sps_regex_search(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 2) vm_runtime_error(vm, 0, "regexSearch() expects 2 arguments");
    Regex* regex = arg_regex(vm, args[0], "regexSearch");
    ObjString* text = arg_text(vm, args[1], "regexSearch");

    ObjList* result = list_new(vm);
    size_t position = 0;
    while (position <= text->len) {
        size_t begin = 0;
        size_t end = 0;
        size_t offsets[REGEX_MAX_OFFSETS];
        for (int i = 0; i < REGEX_MAX_OFFSETS; i++) offsets[i] = REGEX_NO_OFFSET;
        int group_count = 0;
        if (!regex_search(regex, text->chars, text->len, position, &begin, &end,
                          &group_count, offsets)) break;

        ObjList* match = list_new(vm);
        for (int i = 0; i <= group_count; i++) {
            size_t from = offsets[2 * i];
            size_t to = offsets[2 * i + 1];
            ObjString* part;
            if (from == REGEX_NO_OFFSET || to == REGEX_NO_OFFSET) {
                part = string_new_cstr(vm, "");
            } else {
                part = string_new(vm, text->chars + from, to - from);
            }
            list_push(vm, match, value_obj(&part->obj));
        }
        list_push(vm, result, value_obj(&match->obj));

        if (end > begin) {
            position = end;
        } else {
            position = begin + 1;
        }
    }
    return value_obj(&result->obj);
}

// regexReplace(re, text, replacement) - the text with the replaced matches
Value sps_regex_replace(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 3) vm_runtime_error(vm, 0, "regexReplace() expects 3 arguments");
    Regex* regex = arg_regex(vm, args[0], "regexReplace");
    ObjString* text = arg_text(vm, args[1], "regexReplace");
    const char* replacement = arg_string(vm, args[2], "regexReplace");

    char* buffer = NULL;
    size_t length = 0;
    if (regex_replace(regex, text->chars, text->len, replacement, &buffer, &length) != 0) {
        vm_runtime_error(vm, 0, "out of memory");
        return value_nil();
    }

    ObjString* result = string_new(vm, buffer, length);
    free(buffer);
    return value_obj(&result->obj);
}

// regexSplit(re, text) - the list of the parts of the text
Value sps_regex_split(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 2) vm_runtime_error(vm, 0, "regexSplit() expects 2 arguments");
    Regex* regex = arg_regex(vm, args[0], "regexSplit");
    ObjString* text = arg_text(vm, args[1], "regexSplit");

    char** parts = NULL;
    size_t count = 0;
    if (regex_split(regex, text->chars, text->len, &parts, &count) != 0) {
        vm_runtime_error(vm, 0, "out of memory");
        return value_nil();
    }

    ObjList* result = list_new(vm);
    for (size_t i = 0; i < count; i++) {
        ObjString* part = string_new_cstr(vm, parts[i]);
        list_push(vm, result, value_obj(&part->obj));
        free(parts[i]);
    }
    free(parts);
    return value_obj(&result->obj);
}

// regexGroupCount(re) - the number of the capturing groups of the pattern
Value sps_regex_group_count(VM* vm, int argc, Value* args, void* user) {
    (void)user;
    if (argc != 1) vm_runtime_error(vm, 0, "regexGroupCount() expects 1 argument");
    Regex* regex = arg_regex(vm, args[0], "regexGroupCount");
    return value_int(regex_group_count(regex));
}
