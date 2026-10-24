// Spectra3D Script - the SPL format
//
// The parser and the writer of the compact format of Spectra3D. See spl.h for
// the description of the format.

#include "internal/spl.h"
#include "internal/dict.h"
#include "internal/vm.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* text;
    size_t      length;
    size_t      position;
    VM*         vm;
    bool        failed;
} SplParser;

// ------------------------------------------------------------------
// the parser
// ------------------------------------------------------------------

static void skip_space(SplParser* parser) {
    for (;;) {
        while (parser->position < parser->length &&
               isspace((unsigned char)parser->text[parser->position])) {
            parser->position++;
        }
        if (parser->position + 1 < parser->length &&
            parser->text[parser->position] == '/' && parser->text[parser->position + 1] == '/') {
            while (parser->position < parser->length && parser->text[parser->position] != '\n') {
                parser->position++;
            }
            continue;
        }
        if (parser->position + 1 < parser->length &&
            parser->text[parser->position] == '/' && parser->text[parser->position + 1] == '*') {
            parser->position += 2;
            while (parser->position + 1 < parser->length &&
                   !(parser->text[parser->position] == '*' && parser->text[parser->position + 1] == '/')) {
                parser->position++;
            }
            parser->position = (parser->position + 1 < parser->length) ? parser->position + 2
                                                                       : parser->length;
            continue;
        }
        break;
    }
}

static char peek(SplParser* parser) {
    if (parser->position >= parser->length) return '\0';
    return parser->text[parser->position];
}

static Value parse_value(SplParser* parser);

static ObjString* parse_string(SplParser* parser) {
    char quote = parser->text[parser->position++];

    size_t capacity = 32;
    size_t length = 0;
    char* buffer = (char*)malloc(capacity);
    if (!buffer) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }

    while (parser->position < parser->length) {
        char c = parser->text[parser->position++];
        if (c == quote) {
            ObjString* result = string_new(parser->vm, buffer, length);
            free(buffer);
            return result;
        }
        if (c == '\\' && parser->position < parser->length) {
            char escape = parser->text[parser->position++];
            switch (escape) {
                case 'n':  c = '\n'; break;
                case 't':  c = '\t'; break;
                case 'r':  c = '\r'; break;
                case '0':  c = '\0'; break;
                case '\\': c = '\\'; break;
                case '"':  c = '"';  break;
                case '\'': c = '\''; break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                default:
                    buffer[length++] = '\\';
                    c = escape;
                    break;
            }
        }
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
        buffer[length++] = c;
    }

    free(buffer);
    parser->failed = true;
    return string_new_cstr(parser->vm, "");
}

// The name of a key: the letters, the digits, the '_' and the '-'.
static ObjString* parse_key(SplParser* parser) {
    size_t start = parser->position;
    while (parser->position < parser->length) {
        char c = parser->text[parser->position];
        if (isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.') {
            parser->position++;
            continue;
        }
        break;
    }
    if (parser->position == start) {
        parser->failed = true;
        return string_new_cstr(parser->vm, "");
    }
    return string_new(parser->vm, parser->text + start, parser->position - start);
}

static Value parse_object(SplParser* parser) {
    parser->position++; // '{'
    ObjDict* dict = dict_new(parser->vm);

    for (;;) {
        skip_space(parser);
        if (peek(parser) == '}') {
            parser->position++;
            break;
        }
        if (parser->position >= parser->length) {
            parser->failed = true;
            break;
        }

        ObjString* key = parse_key(parser);
        skip_space(parser);
        if (peek(parser) != '=') {
            parser->failed = true;
            break;
        }
        parser->position++;
        skip_space(parser);

        Value value = parse_value(parser);
        dict_set(parser->vm, dict, value_obj(&key->obj), value);

        skip_space(parser);
        if (peek(parser) == ',') parser->position++;
    }

    return value_obj(&dict->obj);
}

static Value parse_array(SplParser* parser) {
    parser->position++; // '['
    ObjList* list = list_new(parser->vm);

    for (;;) {
        skip_space(parser);
        if (peek(parser) == ']') {
            parser->position++;
            break;
        }
        if (parser->position >= parser->length) {
            parser->failed = true;
            break;
        }

        Value value = parse_value(parser);
        list_push(parser->vm, list, value);

        skip_space(parser);
        if (peek(parser) == ',') parser->position++;
    }

    return value_obj(&list->obj);
}

static Value parse_number(SplParser* parser) {
    const char* start = parser->text + parser->position;
    bool is_float = false;

    if (peek(parser) == '-' || peek(parser) == '+') parser->position++;
    if (parser->position + 1 < parser->length &&
        parser->text[parser->position] == '0' &&
        (parser->text[parser->position + 1] == 'x' || parser->text[parser->position + 1] == 'X')) {
        parser->position += 2;
        while (parser->position < parser->length &&
               isxdigit((unsigned char)parser->text[parser->position])) {
            parser->position++;
        }
        char* end = NULL;
        long long number = strtoll(start, &end, 16);
        return value_int((int64_t)number);
    }

    while (parser->position < parser->length) {
        char c = parser->text[parser->position];
        if (c == '.' || c == 'e' || c == 'E') {
            is_float = true;
            parser->position++;
            continue;
        }
        if (c == '-' || c == '+') {
            parser->position++;
            continue;
        }
        if (!isdigit((unsigned char)c)) break;
        parser->position++;
    }

    char* end = NULL;
    if (is_float) {
        double number = strtod(start, &end);
        return value_float(number);
    }
    long long number = strtoll(start, &end, 10);
    return value_int((int64_t)number);
}

static Value parse_value(SplParser* parser) {
    skip_space(parser);
    char c = peek(parser);

    if (c == '{') return parse_object(parser);
    if (c == '[') return parse_array(parser);
    if (c == '"' || c == '\'') {
        ObjString* text = parse_string(parser);
        return value_obj(&text->obj);
    }
    if (c == 't' && parser->position + 4 <= parser->length &&
        memcmp(parser->text + parser->position, "true", 4) == 0) {
        parser->position += 4;
        return value_bool(true);
    }
    if (c == 'f' && parser->position + 5 <= parser->length &&
        memcmp(parser->text + parser->position, "false", 5) == 0) {
        parser->position += 5;
        return value_bool(false);
    }
    if (c == 'n' && parser->position + 3 <= parser->length &&
        (memcmp(parser->text + parser->position, "nil", 3) == 0 ||
         memcmp(parser->text + parser->position, "null", 4) == 0)) {
        parser->position += (parser->text[parser->position + 2] == 'l') ? 3 : 4;
        return value_nil();
    }
    if (c == '-' || c == '+' || isdigit((unsigned char)c)) {
        return parse_number(parser);
    }

    parser->failed = true;
    return value_nil();
}

Value spl_parse(VM* vm, const char* text, size_t length, bool* ok) {
    SplParser parser;
    parser.text = text;
    parser.length = length;
    parser.position = 0;
    parser.vm = vm;
    parser.failed = false;

    Value result = parse_value(&parser);
    skip_space(&parser);
    if (parser.failed || parser.position < parser.length) {
        if (ok) *ok = false;
        return value_nil();
    }
    if (ok) *ok = true;
    return result;
}

// ------------------------------------------------------------------
// the writer
// ------------------------------------------------------------------

typedef struct {
    char*  data;
    size_t length;
    size_t capacity;
} SplWriter;

static void writer_init(SplWriter* writer) {
    writer->capacity = 128;
    writer->length = 0;
    writer->data = (char*)malloc(writer->capacity);
    if (!writer->data) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    writer->data[0] = '\0';
}

static void writer_puts(SplWriter* writer, const char* text) {
    size_t length = strlen(text);
    if (writer->length + length + 1 > writer->capacity) {
        while (writer->length + length + 1 > writer->capacity) writer->capacity *= 2;
        char* grown = (char*)realloc(writer->data, writer->capacity);
        if (!grown) {
            fprintf(stderr, "sps: out of memory\n");
            exit(1);
        }
        writer->data = grown;
    }
    memcpy(writer->data + writer->length, text, length);
    writer->length += length;
    writer->data[writer->length] = '\0';
}

static void writer_putc(SplWriter* writer, char c) {
    char buffer[2] = { c, '\0' };
    writer_puts(writer, buffer);
}

// True when the text can be a key of the SPL format without the quotes.
static bool is_identifier(const char* text, size_t length) {
    if (length == 0) return false;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        if (isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.') continue;
        return false;
    }
    return !isdigit((unsigned char)text[0]);
}

// The key of a member as a text.
static const char* dict_key_text(ObjDict* dict, uint32_t index) {
    if (dict->entries[index].key.type != VAL_STRING) return "";
    return ((ObjString*)dict->entries[index].key.as.obj)->chars;
}

static void write_string(SplWriter* writer, const char* text, size_t length) {
    writer_putc(writer, '"');
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        switch (c) {
            case '"':  writer_puts(writer, "\\\""); break;
            case '\\': writer_puts(writer, "\\\\"); break;
            case '\n': writer_puts(writer, "\\n"); break;
            case '\t': writer_puts(writer, "\\t"); break;
            case '\r': writer_puts(writer, "\\r"); break;
            default:   writer_putc(writer, c); break;
        }
    }
    writer_putc(writer, '"');
}

static void write_indent(SplWriter* writer, bool pretty, int indent, int level) {
    if (!pretty) return;
    writer_putc(writer, '\n');
    for (int i = 0; i < indent * level; i++) writer_putc(writer, ' ');
}

static void write_value(SplWriter* writer, Value value, bool pretty, int indent, int level) {
    switch (value.type) {
        case VAL_NIL:
            writer_puts(writer, "nil");
            break;
        case VAL_BOOL:
            writer_puts(writer, value.as.boolean ? "true" : "false");
            break;
        case VAL_INT: {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%lld", (long long)value.as.integer);
            writer_puts(writer, buffer);
            break;
        }
        case VAL_FLOAT:
        case VAL_FLOAT32: {
            char buffer[64];
            double number = value.as.number;
            for (int precision = 15; precision <= 17; precision++) {
                snprintf(buffer, sizeof(buffer), "%.*g", precision, number);
                if (strtod(buffer, NULL) == number) break;
            }
            writer_puts(writer, buffer);
            break;
        }
        case VAL_STRING: {
            ObjString* text = (ObjString*)value.as.obj;
            write_string(writer, text->chars, text->len);
            break;
        }
        case VAL_LIST: {
            ObjList* list = (ObjList*)value.as.obj;
            writer_putc(writer, '[');
            for (uint32_t i = 0; i < list->count; i++) {
                if (i) writer_putc(writer, ',');
                write_indent(writer, pretty, indent, level + 1);
                write_value(writer, list->items[i], pretty, indent, level + 1);
            }
            if (list->count) write_indent(writer, pretty, indent, level);
            writer_putc(writer, ']');
            break;
        }
        case VAL_DICT: {
            ObjDict* dict = (ObjDict*)value.as.obj;
            writer_putc(writer, '{');

            // the members are written in the order of their keys, so the
            // result is the same everywhere
            uint32_t* order = (uint32_t*)malloc(sizeof(uint32_t) * (dict->count + 1));
            uint32_t order_count = 0;
            if (order) {
                for (uint32_t i = 0; i < dict->capacity; i++) {
                    if (dict->entries[i].state == 1) order[order_count++] = i;
                }
                for (uint32_t i = 1; i < order_count; i++) {
                    uint32_t index = order[i];
                    const char* key = dict_key_text(dict, index);
                    uint32_t j = i;
                    while (j > 0 && strcmp(key, dict_key_text(dict, order[j - 1])) < 0) {
                        order[j] = order[j - 1];
                        j--;
                    }
                    order[j] = index;
                }
            }

            for (uint32_t n = 0; n < order_count; n++) {
                uint32_t i = order[n];
                if (n) writer_putc(writer, ',');
                write_indent(writer, pretty, indent, level + 1);
                if (dict->entries[i].key.type == VAL_STRING) {
                    ObjString* key = (ObjString*)dict->entries[i].key.as.obj;
                    if (is_identifier(key->chars, key->len)) {
                        writer_puts(writer, key->chars);
                    } else {
                        write_string(writer, key->chars, key->len);
                    }
                } else {
                    write_value(writer, dict->entries[i].key, pretty, indent, level + 1);
                }
                writer_puts(writer, pretty ? " = " : "=");
                write_value(writer, dict->entries[i].value, pretty, indent, level + 1);
            }
            if (order_count) write_indent(writer, pretty, indent, level);
            writer_putc(writer, '}');
            free(order);
            break;
        }
        default:
            writer_puts(writer, "nil");
            break;
    }
}

ObjString* spl_stringify(VM* vm, Value value, bool pretty, int indent) {
    SplWriter writer;
    writer_init(&writer);
    write_value(&writer, value, pretty, indent, 0);
    ObjString* result = string_new(vm, writer.data, writer.length);
    free(writer.data);
    return result;
}
