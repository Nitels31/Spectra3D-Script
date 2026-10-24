// Spectra3D Script - the regular expressions (the engine)
//
// The pattern is parsed into a tree of the nodes, the tree is compiled into
// a bytecode and the matcher walks the bytecode with an explicit stack.

#include "internal/regex.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------
// the limits
// ------------------------------------------------------------------

// The size of the stack of the backtracking: the engine stops when it is
// full instead of eating the memory of the process.
#define REGEX_MAX_THREADS 20000

// The number of the instructions of one attempt of the match: it protects
// the program from the patterns that explode the number of the states.
#define REGEX_MAX_STEPS 4000000

// ------------------------------------------------------------------
// the character classes
// ------------------------------------------------------------------

typedef struct {
    bool          negated;
    unsigned char bits[32];  // 256 bits
} CharClass;

static void class_clear(CharClass* klass) {
    memset(klass->bits, 0, sizeof(klass->bits));
}

static void class_add(CharClass* klass, unsigned char c) {
    klass->bits[c >> 3] |= (unsigned char)(1u << (c & 7));
}

static void class_add_range(CharClass* klass, unsigned char from, unsigned char to) {
    for (unsigned int c = from; c <= to; c++) class_add(klass, (unsigned char)c);
}

// Adds the characters of the source to the target: the negated source is
// added with the opposite sense.
static void class_merge(CharClass* target, const CharClass* source) {
    for (int i = 0; i < 32; i++) {
        if (source->negated) {
            target->bits[i] |= (unsigned char)~source->bits[i];
        } else {
            target->bits[i] |= source->bits[i];
        }
    }
}

static bool class_has(const CharClass* klass, unsigned char c) {
    bool found = (klass->bits[c >> 3] & (1u << (c & 7))) != 0;
    return klass->negated ? !found : found;
}

// ------------------------------------------------------------------
// the tree of the nodes
// ------------------------------------------------------------------

typedef enum {
    NODE_CHAR,     // a single character
    NODE_CLASS,    // a character class
    NODE_ANY,      // the dot
    NODE_BOL,      // the anchor ^
    NODE_EOL,      // the anchor $
    NODE_SEQ,      // a sequence of the nodes
    NODE_ALT,      // an alternation
    NODE_GROUP,    // a group: the capturing one or not
    NODE_REPEAT,   // a quantified node
    NODE_BACKREF   // a back reference
} NodeType;

typedef struct Node Node;
struct Node {
    NodeType      type;
    unsigned char ch;
    int           class_index;
    int           group;
    Node**        items;
    int           count;
    Node*         child;
    int           min;
    int           max;     // -1 means "without the limit"
    bool          lazy;
};

// ------------------------------------------------------------------
// the parser
// ------------------------------------------------------------------

typedef struct {
    const char* pattern;
    size_t      position;
    size_t      length;
    char*       error;
    size_t      error_size;
    bool        failed;
    int         group_count;
    Node**      nodes;      // the pool of the nodes: it is released at the end
    int         node_count;
    int         node_cap;
    CharClass*  classes;
    int         class_count;
    int         class_cap;
} Parser;

static void parser_error(Parser* parser, const char* format, ...) {
    if (parser->failed) return;
    parser->failed = true;
    if (!parser->error || parser->error_size == 0) return;
    va_list args;
    va_start(args, format);
    vsnprintf(parser->error, parser->error_size, format, args);
    va_end(args);
}

static Node* node_new(Parser* parser, NodeType type) {
    if (parser->node_count == parser->node_cap) {
        int capacity = parser->node_cap ? parser->node_cap * 2 : 32;
        Node** nodes = (Node**)realloc(parser->nodes, sizeof(Node*) * (size_t)capacity);
        if (!nodes) {
            parser_error(parser, "out of memory");
            return NULL;
        }
        parser->nodes = nodes;
        parser->node_cap = capacity;
    }
    Node* node = (Node*)calloc(1, sizeof(Node));
    if (!node) {
        parser_error(parser, "out of memory");
        return NULL;
    }
    node->type = type;
    node->max = -1;
    parser->nodes[parser->node_count++] = node;
    return node;
}

// Adds a node to the list of the nodes.
static bool node_append(Node* list, Node* item) {
    Node** items = (Node**)realloc(list->items, sizeof(Node*) * (size_t)(list->count + 1));
    if (!items) return false;
    list->items = items;
    list->items[list->count++] = item;
    return true;
}

// Releases the tree of the nodes.
static void parser_release(Parser* parser) {
    for (int i = 0; i < parser->node_count; i++) {
        free(parser->nodes[i]->items);
        free(parser->nodes[i]);
    }
    free(parser->nodes);
    parser->nodes = NULL;
    parser->node_count = 0;
    parser->node_cap = 0;
}

// Adds a character class to the table of the classes.
static int class_add_to_parser(Parser* parser, const CharClass* klass) {
    if (parser->class_count == parser->class_cap) {
        int capacity = parser->class_cap ? parser->class_cap * 2 : 16;
        CharClass* classes = (CharClass*)realloc(parser->classes,
                                                 sizeof(CharClass) * (size_t)capacity);
        if (!classes) {
            parser_error(parser, "out of memory");
            return -1;
        }
        parser->classes = classes;
        parser->class_cap = capacity;
    }
    parser->classes[parser->class_count] = *klass;
    return parser->class_count++;
}

static bool parser_at_end(const Parser* parser) {
    return parser->position >= parser->length;
}

static char parser_peek(const Parser* parser) {
    return parser_at_end(parser) ? '\0' : parser->pattern[parser->position];
}

// ------------------------------------------------------------------
// the parser: the escapes and the classes
// ------------------------------------------------------------------

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static Node* node_char(Parser* parser, unsigned char c) {
    Node* node = node_new(parser, NODE_CHAR);
    if (node) node->ch = c;
    return node;
}

// Reads the escape sequence: the backslash is already consumed.
static Node* parse_escape(Parser* parser) {
    if (parser_at_end(parser)) {
        parser_error(parser, "the escape sequence is not finished");
        return NULL;
    }
    char c = parser->pattern[parser->position++];
    switch (c) {
        case 'd': case 'D': case 'w': case 'W': case 's': case 'S': {
            CharClass klass;
            class_clear(&klass);
            if (c == 'd' || c == 'D') {
                class_add_range(&klass, '0', '9');
            } else if (c == 'w' || c == 'W') {
                class_add_range(&klass, 'a', 'z');
                class_add_range(&klass, 'A', 'Z');
                class_add_range(&klass, '0', '9');
                class_add(&klass, '_');
            } else {
                class_add(&klass, ' ');
                class_add(&klass, '\t');
                class_add(&klass, '\n');
                class_add(&klass, '\r');
                class_add(&klass, '\f');
                class_add(&klass, '\v');
            }
            klass.negated = (c == 'D' || c == 'W' || c == 'S');
            int index = class_add_to_parser(parser, &klass);
            if (index < 0) return NULL;
            Node* node = node_new(parser, NODE_CLASS);
            if (node) node->class_index = index;
            return node;
        }
        case 'n': return node_char(parser, '\n');
        case 't': return node_char(parser, '\t');
        case 'r': return node_char(parser, '\r');
        case 'f': return node_char(parser, '\f');
        case 'v': return node_char(parser, '\v');
        case '0': return node_char(parser, '\0');
        case 'x': {
            if (parser->position + 1 >= parser->length) {
                parser_error(parser, "the hexadecimal escape is not finished");
                return NULL;
            }
            int high = hex_value(parser->pattern[parser->position]);
            int low = hex_value(parser->pattern[parser->position + 1]);
            if (high < 0 || low < 0) {
                parser_error(parser, "the hexadecimal escape is broken");
                return NULL;
            }
            parser->position += 2;
            return node_char(parser, (unsigned char)((high << 4) | low));
        }
        case '1': case '2': case '3': case '4': case '5':
        case '6': case '7': case '8': case '9': {
            Node* node = node_new(parser, NODE_BACKREF);
            if (node) node->group = c - '0';
            return node;
        }
        case '\\': case '.': case '*': case '+': case '?': case '(': case ')':
        case '[': case ']': case '{': case '}': case '|': case '^': case '$':
        case '-': case '/': case '#': case '!': case '%': case '&':
            return node_char(parser, (unsigned char)c);
        default:
            parser_error(parser, "unknown escape sequence '\\%c'", c);
            return NULL;
    }
}

// Reads the character class: the opening bracket is already consumed.
static Node* parse_class(Parser* parser) {
    CharClass klass;
    class_clear(&klass);

    bool negated = false;
    if (parser_peek(parser) == '^') {
        negated = true;
        parser->position++;
    }

    bool first = true;
    for (;;) {
        if (parser_at_end(parser)) {
            parser_error(parser, "the character class is not closed");
            return NULL;
        }
        char c = parser->pattern[parser->position];
        if (c == ']' && !first) {
            parser->position++;
            break;
        }
        first = false;
        parser->position++;

        unsigned char from = 0;
        if (c == '\\') {
            Node* item = parse_escape(parser);
            if (!item) return NULL;
            if (item->type == NODE_CLASS) {
                class_merge(&klass, &parser->classes[item->class_index]);
                continue;
            }
            if (item->type != NODE_CHAR) {
                parser_error(parser, "the escape is not allowed in a character class");
                return NULL;
            }
            from = item->ch;
        } else {
            from = (unsigned char)c;
        }

        // the range: "a-z"
        bool is_range = parser_peek(parser) == '-' &&
                        parser->position + 1 < parser->length &&
                        parser->pattern[parser->position + 1] != ']';
        if (is_range) {
            parser->position++;
            char d = parser->pattern[parser->position++];
            unsigned char to = 0;
            if (d == '\\') {
                Node* item = parse_escape(parser);
                if (!item) return NULL;
                if (item->type != NODE_CHAR) {
                    parser_error(parser, "the escape is not allowed in a character class");
                    return NULL;
                }
                to = item->ch;
            } else {
                to = (unsigned char)d;
            }
            if (to < from) {
                parser_error(parser, "the range of the character class is broken");
                return NULL;
            }
            class_add_range(&klass, from, to);
        } else {
            class_add(&klass, from);
        }
    }

    klass.negated = negated;
    int index = class_add_to_parser(parser, &klass);
    if (index < 0) return NULL;
    Node* node = node_new(parser, NODE_CLASS);
    if (node) node->class_index = index;
    return node;
}

// ------------------------------------------------------------------
// the parser: the groups, the quantifiers and the pattern
// ------------------------------------------------------------------

static Node* parse_alternation(Parser* parser);

static Node* parse_group(Parser* parser) {
    int group = 0;
    if (parser_peek(parser) == '?') {
        if (parser->position + 1 < parser->length &&
            parser->pattern[parser->position + 1] == ':') {
            parser->position += 2;   // the group without the capture
        } else {
            parser_error(parser, "the modifier of the group is not supported");
            return NULL;
        }
    } else {
        if (parser->group_count >= REGEX_MAX_GROUPS) {
            parser_error(parser, "the pattern has too many groups (the maximum is %d)",
                         REGEX_MAX_GROUPS);
            return NULL;
        }
        group = ++parser->group_count;
    }

    Node* child = parse_alternation(parser);
    if (!child) return NULL;
    if (parser_peek(parser) != ')') {
        parser_error(parser, "the group is not closed");
        return NULL;
    }
    parser->position++;

    Node* node = node_new(parser, NODE_GROUP);
    if (!node) return NULL;
    node->group = group;
    node->child = child;
    return node;
}

static Node* parse_atom(Parser* parser) {
    if (parser_at_end(parser)) return NULL;
    char c = parser->pattern[parser->position++];
    switch (c) {
        case '(': return parse_group(parser);
        case '[': return parse_class(parser);
        case '\\': return parse_escape(parser);
        case '.': {
            Node* node = node_new(parser, NODE_ANY);
            return node;
        }
        case '^': {
            Node* node = node_new(parser, NODE_BOL);
            return node;
        }
        case '$': {
            Node* node = node_new(parser, NODE_EOL);
            return node;
        }
        case '*': case '+': case '?':
            parser_error(parser, "the quantifier has nothing to repeat");
            return NULL;
        case ')':
            parser_error(parser, "the group is not closed");
            return NULL;
        default:
            return node_char(parser, (unsigned char)c);
    }
}

// Reads the counters of the quantifier: "{n}", "{n,}" and "{n,m}". The
// function returns false when the braces are not a counter: then the brace
// is a literal character.
static bool parse_counters(Parser* parser, int* min, int* max) {
    size_t start = parser->position;
    int value = 0;
    bool digits = false;
    while (!parser_at_end(parser) &&
           isdigit((unsigned char)parser->pattern[parser->position])) {
        value = value * 10 + (parser->pattern[parser->position] - '0');
        if (value > 100000) {
            parser_error(parser, "the counter of the quantifier is too big");
            return false;
        }
        parser->position++;
        digits = true;
    }
    if (!digits) {
        parser->position = start;
        return false;
    }
    *min = value;

    if (parser_peek(parser) == '}') {
        parser->position++;
        *max = value;
        return true;
    }
    if (parser_peek(parser) == ',') {
        parser->position++;
        if (parser_peek(parser) == '}') {
            parser->position++;
            *max = -1;   // without the limit
            return true;
        }
        int limit = 0;
        bool has_digits = false;
        while (!parser_at_end(parser) &&
               isdigit((unsigned char)parser->pattern[parser->position])) {
            limit = limit * 10 + (parser->pattern[parser->position] - '0');
            if (limit > 100000) {
                parser_error(parser, "the counter of the quantifier is too big");
                return false;
            }
            parser->position++;
            has_digits = true;
        }
        if (has_digits && parser_peek(parser) == '}') {
            parser->position++;
            if (limit < *min) {
                parser_error(parser, "the counter of the quantifier is broken");
                return false;
            }
            *max = limit;
            return true;
        }
    }
    parser->position = start;
    return false;
}

static Node* parse_repeat(Parser* parser) {
    Node* atom = parse_atom(parser);
    if (!atom) return NULL;

    int min = 0;
    int max = 0;
    char c = parser_peek(parser);
    if (c == '*') {
        min = 0;
        max = -1;
        parser->position++;
    } else if (c == '+') {
        min = 1;
        max = -1;
        parser->position++;
    } else if (c == '?') {
        min = 0;
        max = 1;
        parser->position++;
    } else if (c == '{') {
        size_t save = parser->position;
        parser->position++;
        if (!parse_counters(parser, &min, &max)) {
            if (parser->failed) return NULL;
            parser->position = save;   // the brace is a literal character
            return atom;
        }
    } else {
        return atom;
    }

    bool lazy = false;
    if (parser_peek(parser) == '?') {
        lazy = true;
        parser->position++;
    }

    // the quantifier cannot be repeated: "a**" is broken
    char next = parser_peek(parser);
    if (next == '*' || next == '+' || next == '?' || next == '{') {
        parser_error(parser, "the quantifier cannot be repeated");
        return NULL;
    }

    Node* node = node_new(parser, NODE_REPEAT);
    if (!node) return NULL;
    node->child = atom;
    node->min = min;
    node->max = max;
    node->lazy = lazy;
    return node;
}

static Node* parse_sequence(Parser* parser) {
    Node* seq = node_new(parser, NODE_SEQ);
    if (!seq) return NULL;
    for (;;) {
        if (parser_at_end(parser)) break;
        char c = parser->pattern[parser->position];
        if (c == '|' || c == ')') break;
        Node* item = parse_repeat(parser);
        if (!item) return NULL;
        if (!node_append(seq, item)) {
            parser_error(parser, "out of memory");
            return NULL;
        }
    }
    return seq;
}

static Node* parse_alternation(Parser* parser) {
    Node* alt = node_new(parser, NODE_ALT);
    if (!alt) return NULL;
    for (;;) {
        Node* seq = parse_sequence(parser);
        if (!seq) return NULL;
        if (!node_append(alt, seq)) {
            parser_error(parser, "out of memory");
            return NULL;
        }
        if (parser_peek(parser) != '|') break;
        parser->position++;
    }
    if (alt->count == 1) return alt->items[0];
    return alt;
}

// Checks the back references: they have to point to an existing group.
static bool check_backrefs(Parser* parser, const Node* node) {
    if (node->type == NODE_BACKREF && node->group > parser->group_count) {
        parser_error(parser, "the back reference '\\%d' has no group", node->group);
        return false;
    }
    for (int i = 0; i < node->count; i++) {
        if (!check_backrefs(parser, node->items[i])) return false;
    }
    if (node->child && !check_backrefs(parser, node->child)) return false;
    return true;
}

// ------------------------------------------------------------------
// the bytecode
// ------------------------------------------------------------------

typedef enum {
    OP_CHAR,     // a = the code of the character
    OP_CLASS,    // a = the index of the class
    OP_ANY,      // any character except the newline
    OP_SPLIT,    // a = the first branch, b = the second branch
    OP_JMP,      // a = the target
    OP_SAVE,     // a = the slot of the group
    OP_SET,      // a = the slot of the check
    OP_CHECK,    // the match fails when the position did not move
    OP_BOL,      // the beginning of the text
    OP_EOL,      // the end of the text
    OP_BACKREF,  // a = the number of the group
    OP_MATCH     // the match is complete
} OpCode;

typedef struct {
    uint8_t op;
    int32_t a;
    int32_t b;
} Insn;

typedef struct {
    Insn*  code;
    int    count;
    int    cap;
    bool   failed;
    char*  error;
    size_t error_size;
    int    check_base;   // the first slot of the checks: the groups are before
    int    check_count;
} Compiler;

static void compiler_error(Compiler* compiler, const char* message) {
    if (compiler->failed) return;
    compiler->failed = true;
    if (!compiler->error || compiler->error_size == 0) return;
    snprintf(compiler->error, compiler->error_size, "%s", message);
}

static int emit(Compiler* compiler, int op, int a, int b) {
    if (compiler->count == compiler->cap) {
        int capacity = compiler->cap ? compiler->cap * 2 : 32;
        Insn* code = (Insn*)realloc(compiler->code, sizeof(Insn) * (size_t)capacity);
        if (!code) {
            compiler_error(compiler, "out of memory");
            return -1;
        }
        compiler->code = code;
        compiler->cap = capacity;
    }
    Insn* insn = &compiler->code[compiler->count];
    insn->op = (uint8_t)op;
    insn->a = a;
    insn->b = b;
    return compiler->count++;
}

static bool compile_node(Compiler* compiler, const Node* node);

static bool compile_repeat(Compiler* compiler, const Node* node) {
    const Node* child = node->child;
    int min = node->min;
    int max = node->max;

    // the mandatory copies
    for (int i = 0; i < min; i++) {
        if (!compile_node(compiler, child)) return false;
    }

    if (max < 0) {
        // the loop without the limit: the check of the progress stops the
        // repetitions that do not move the position (the pattern "(a*)*")
        int slot = compiler->check_base + compiler->check_count++;
        int loop = emit(compiler, OP_SET, slot, 0);
        if (loop < 0) return false;
        int split = emit(compiler, OP_SPLIT, 0, 0);
        if (split < 0) return false;
        if (!compile_node(compiler, child)) return false;
        if (emit(compiler, OP_CHECK, slot, 0) < 0) return false;
        if (emit(compiler, OP_JMP, loop, 0) < 0) return false;
        int end = compiler->count;
        if (node->lazy) {
            compiler->code[split].a = end;         // the exit is the first
            compiler->code[split].b = split + 1;   // then the body
        } else {
            compiler->code[split].a = split + 1;   // the body is the first
            compiler->code[split].b = end;         // then the exit
        }
        return true;
    }

    // the optional copies
    for (int i = min; i < max; i++) {
        int split = emit(compiler, OP_SPLIT, 0, 0);
        if (split < 0) return false;
        if (!compile_node(compiler, child)) return false;
        int end = compiler->count;
        if (node->lazy) {
            compiler->code[split].a = end;
            compiler->code[split].b = split + 1;
        } else {
            compiler->code[split].a = split + 1;
            compiler->code[split].b = end;
        }
    }
    return true;
}

static bool compile_node(Compiler* compiler, const Node* node) {
    switch (node->type) {
        case NODE_CHAR:
            return emit(compiler, OP_CHAR, node->ch, 0) >= 0;
        case NODE_CLASS:
            return emit(compiler, OP_CLASS, node->class_index, 0) >= 0;
        case NODE_ANY:
            return emit(compiler, OP_ANY, 0, 0) >= 0;
        case NODE_BOL:
            return emit(compiler, OP_BOL, 0, 0) >= 0;
        case NODE_EOL:
            return emit(compiler, OP_EOL, 0, 0) >= 0;
        case NODE_BACKREF:
            return emit(compiler, OP_BACKREF, node->group, 0) >= 0;
        case NODE_SEQ: {
            for (int i = 0; i < node->count; i++) {
                if (!compile_node(compiler, node->items[i])) return false;
            }
            return true;
        }
        case NODE_ALT: {
            int count = node->count;
            int* jumps = (int*)malloc(sizeof(int) * (size_t)(count > 0 ? count : 1));
            if (!jumps) {
                compiler_error(compiler, "out of memory");
                return false;
            }
            for (int i = 0; i < count - 1; i++) {
                int split = emit(compiler, OP_SPLIT, 0, 0);
                if (split < 0) { free(jumps); return false; }
                if (!compile_node(compiler, node->items[i])) { free(jumps); return false; }
                int jump = emit(compiler, OP_JMP, 0, 0);
                if (jump < 0) { free(jumps); return false; }
                jumps[i] = jump;
                compiler->code[split].a = split + 1;   // the first alternative
                compiler->code[split].b = compiler->count;
            }
            if (count > 0 && !compile_node(compiler, node->items[count - 1])) {
                free(jumps);
                return false;
            }
            int end = compiler->count;
            for (int i = 0; i < count - 1; i++) compiler->code[jumps[i]].a = end;
            free(jumps);
            return true;
        }
        case NODE_GROUP: {
            if (node->group > 0) {
                if (emit(compiler, OP_SAVE, 2 * node->group, 0) < 0) return false;
            }
            if (!compile_node(compiler, node->child)) return false;
            if (node->group > 0) {
                if (emit(compiler, OP_SAVE, 2 * node->group + 1, 0) < 0) return false;
            }
            return true;
        }
        case NODE_REPEAT:
            return compile_repeat(compiler, node);
    }
    compiler_error(compiler, "the pattern is broken");
    return false;
}

// ------------------------------------------------------------------
// the compiled pattern
// ------------------------------------------------------------------

struct Regex {
    char*      source;
    Insn*      code;
    int        code_count;
    CharClass* classes;
    int        class_count;
    int        group_count;
    int        slot_count;
    bool       anchored;   // the pattern begins with '^'
};

// ------------------------------------------------------------------
// the matcher
// ------------------------------------------------------------------

typedef struct {
    int    pc;
    size_t sp;
} Thread;

typedef struct {
    Thread* threads;
    size_t* slots;      // the copy of the slots of every thread
    size_t* current;    // the slots of the thread that is running
    int    count;
    int    cap;
    int    slot_count;
} Backtrack;

static bool backtrack_init(Backtrack* stack, int slot_count) {
    stack->cap = 32;
    stack->count = 0;
    stack->slot_count = slot_count;
    stack->threads = (Thread*)malloc(sizeof(Thread) * (size_t)stack->cap);
    stack->slots = (size_t*)malloc(sizeof(size_t) * (size_t)stack->cap * (size_t)slot_count);
    stack->current = (size_t*)malloc(sizeof(size_t) * (size_t)slot_count);
    if (!stack->threads || !stack->slots || !stack->current) {
        free(stack->threads);
        free(stack->slots);
        free(stack->current);
        stack->threads = NULL;
        stack->slots = NULL;
        stack->current = NULL;
        return false;
    }
    return true;
}

static void backtrack_free(Backtrack* stack) {
    free(stack->threads);
    free(stack->slots);
    free(stack->current);
    stack->threads = NULL;
    stack->slots = NULL;
    stack->current = NULL;
    stack->count = 0;
    stack->cap = 0;
}

// Saves the current state of the matcher on the stack.
static bool backtrack_push(Backtrack* stack, int pc, size_t sp) {
    if (stack->count == stack->cap) {
        if (stack->cap >= REGEX_MAX_THREADS) return false;
        int capacity = stack->cap * 2;
        if (capacity > REGEX_MAX_THREADS) capacity = REGEX_MAX_THREADS;
        Thread* threads = (Thread*)realloc(stack->threads,
                                           sizeof(Thread) * (size_t)capacity);
        if (!threads) return false;
        stack->threads = threads;
        size_t* slots = (size_t*)realloc(stack->slots,
                                         sizeof(size_t) * (size_t)capacity *
                                         (size_t)stack->slot_count);
        if (!slots) return false;
        stack->slots = slots;
        stack->cap = capacity;
    }
    Thread* thread = &stack->threads[stack->count];
    thread->pc = pc;
    thread->sp = sp;
    memcpy(&stack->slots[(size_t)stack->count * (size_t)stack->slot_count],
           stack->current, sizeof(size_t) * (size_t)stack->slot_count);
    stack->count++;
    return true;
}

// Runs the bytecode at the position 'start'. When 'full' is true the match
// has to cover the text up to its end.
static bool regex_exec(Regex* regex, Backtrack* stack, const char* text,
                       size_t length, size_t start, bool full,
                       size_t* match_begin, size_t* match_end, size_t* groups) {
    for (int i = 0; i < regex->slot_count; i++) stack->current[i] = REGEX_NO_OFFSET;

    int pc = 0;
    size_t sp = start;
    long steps = 0;

    for (;;) {
        if (++steps > REGEX_MAX_STEPS) return false;
        const Insn* insn = &regex->code[pc];
        bool failed = false;

        switch (insn->op) {
            case OP_CHAR:
                if (sp < length && (unsigned char)text[sp] == (unsigned char)insn->a) {
                    sp++;
                    pc++;
                } else {
                    failed = true;
                }
                break;
            case OP_CLASS:
                if (sp >= length || !class_has(&regex->classes[insn->a],
                                               (unsigned char)text[sp])) {
                    failed = true;
                } else {
                    sp++;
                    pc++;
                }
                break;
            case OP_ANY:
                if (sp < length && text[sp] != '\n') {
                    sp++;
                    pc++;
                } else {
                    failed = true;
                }
                break;
            case OP_SPLIT:
                if (!backtrack_push(stack, insn->b, sp)) return false;
                pc = insn->a;
                break;
            case OP_JMP:
                pc = insn->a;
                break;
            case OP_SAVE:
            case OP_SET:
                stack->current[insn->a] = sp;
                pc++;
                break;
            case OP_CHECK:
                // the repetition that does not move the position fails: it
                // stops the loop of the empty repetitions
                if (stack->current[insn->a] == sp) failed = true;
                else pc++;
                break;
            case OP_BOL:
                if (sp == 0) pc++;
                else failed = true;
                break;
            case OP_EOL:
                if (sp == length) pc++;
                else failed = true;
                break;
            case OP_BACKREF: {
                int group = insn->a;
                size_t from = stack->current[2 * group];
                size_t to = stack->current[2 * group + 1];
                if (from == REGEX_NO_OFFSET || to == REGEX_NO_OFFSET) {
                    pc++;   // the group did not take part in the match
                    break;
                }
                size_t size = to - from;
                if (sp + size > length || memcmp(text + sp, text + from, size) != 0) {
                    failed = true;
                } else {
                    sp += size;
                    pc++;
                }
                break;
            }
            case OP_MATCH:
                if (full && sp != length) {
                    failed = true;
                    break;
                }
                if (match_begin) *match_begin = stack->current[0];
                if (match_end) *match_end = sp;
                if (groups) {
                    for (int i = 0; i < 2 * (regex->group_count + 1); i++) {
                        groups[i] = stack->current[i];
                    }
                }
                return true;
        }

        if (!failed) continue;

        // the branch failed: the matcher returns to the last alternative
        if (stack->count == 0) return false;
        stack->count--;
        pc = stack->threads[stack->count].pc;
        sp = stack->threads[stack->count].sp;
        memcpy(stack->current,
               &stack->slots[(size_t)stack->count * (size_t)stack->slot_count],
               sizeof(size_t) * (size_t)stack->slot_count);
    }
}

// ------------------------------------------------------------------
// the public interface
// ------------------------------------------------------------------

Regex* regex_compile(const char* pattern, char* error, size_t error_size) {
    if (error && error_size > 0) error[0] = '\0';
    if (!pattern) {
        if (error && error_size > 0) snprintf(error, error_size, "the pattern is missing");
        return NULL;
    }

    Parser parser;
    memset(&parser, 0, sizeof(parser));
    parser.pattern = pattern;
    parser.length = strlen(pattern);
    parser.error = error;
    parser.error_size = error_size;

    Node* root = parse_alternation(&parser);
    if (!root || parser.failed) {
        if (!parser.failed && error && error_size > 0) {
            snprintf(error, error_size, "the pattern is broken");
        }
        parser_release(&parser);
        free(parser.classes);
        return NULL;
    }

    if (parser.position != parser.length) {
        parser_error(&parser, "the pattern has the unexpected characters");
        parser_release(&parser);
        free(parser.classes);
        return NULL;
    }

    if (!check_backrefs(&parser, root)) {
        parser_release(&parser);
        free(parser.classes);
        return NULL;
    }

    Compiler compiler;
    memset(&compiler, 0, sizeof(compiler));
    compiler.error = error;
    compiler.error_size = error_size;
    // the slots of the groups come first: the checks of the repetitions are
    // after them
    compiler.check_base = 2 * (parser.group_count + 1);

    bool ok = emit(&compiler, OP_SAVE, 0, 0) >= 0 &&
              compile_node(&compiler, root) &&
              emit(&compiler, OP_SAVE, 1, 0) >= 0 &&
              emit(&compiler, OP_MATCH, 0, 0) >= 0;
    if (!ok) {
        if (!compiler.failed && error && error_size > 0) {
            snprintf(error, error_size, "the pattern is broken");
        }
        free(compiler.code);
        parser_release(&parser);
        free(parser.classes);
        return NULL;
    }

    Regex* regex = (Regex*)calloc(1, sizeof(Regex));
    if (!regex) {
        if (error && error_size > 0) snprintf(error, error_size, "out of memory");
        free(compiler.code);
        parser_release(&parser);
        free(parser.classes);
        return NULL;
    }

    regex->source = strdup(pattern);
    regex->code = compiler.code;
    regex->code_count = compiler.count;
    regex->classes = parser.classes;
    regex->class_count = parser.class_count;
    regex->group_count = parser.group_count;
    regex->slot_count = 2 * (parser.group_count + 1) + compiler.check_count;
    // the first instruction of the pattern is behind the save of the begin
    regex->anchored = compiler.count > 1 && compiler.code[1].op == OP_BOL;

    parser_release(&parser);
    return regex;
}

void regex_free(Regex* regex) {
    if (!regex) return;
    free(regex->source);
    free(regex->code);
    free(regex->classes);
    free(regex);
}

int regex_group_count(const Regex* regex) {
    return regex ? regex->group_count : 0;
}

bool regex_match(Regex* regex, const char* text, size_t length,
                 size_t* match_begin, size_t* match_end) {
    if (!regex || !text) return false;
    Backtrack stack;
    if (!backtrack_init(&stack, regex->slot_count)) return false;
    bool found = regex_exec(regex, &stack, text, length, 0, true,
                             match_begin, match_end, NULL);
    backtrack_free(&stack);
    return found;
}

bool regex_search(Regex* regex, const char* text, size_t length, size_t from,
                  size_t* match_begin, size_t* match_end,
                  int* group_count, size_t* group_offsets) {
    if (!regex || !text) return false;
    if (from > length) from = length;

    Backtrack stack;
    if (!backtrack_init(&stack, regex->slot_count)) return false;

    bool found = false;
    size_t position = from;
    for (;;) {
        size_t begin = 0;
        size_t end = 0;
        if (regex_exec(regex, &stack, text, length, position, false,
                       &begin, &end, group_offsets)) {
            if (match_begin) *match_begin = begin;
            if (match_end) *match_end = end;
            if (group_count) *group_count = regex->group_count;
            found = true;
            break;
        }
        // the pattern begins with '^': it can only match at the beginning
        if (regex->anchored || position >= length) break;
        position++;
    }

    backtrack_free(&stack);
    return found;
}

// ------------------------------------------------------------------
// the text buffer
// ------------------------------------------------------------------

typedef struct {
    char*  data;
    size_t length;
    size_t capacity;
} TextBuffer;

static void buffer_init(TextBuffer* buffer) {
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

static bool buffer_reserve(TextBuffer* buffer, size_t extra) {
    if (buffer->length + extra + 1 <= buffer->capacity) return true;
    size_t capacity = buffer->capacity ? buffer->capacity : 32;
    while (capacity < buffer->length + extra + 1) capacity *= 2;
    char* data = (char*)realloc(buffer->data, capacity);
    if (!data) return false;
    buffer->data = data;
    buffer->capacity = capacity;
    return true;
}

static bool buffer_append(TextBuffer* buffer, const char* text, size_t length) {
    if (!buffer_reserve(buffer, length)) return false;
    if (length) memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

// ------------------------------------------------------------------
// the replacement and the split
// ------------------------------------------------------------------

// Writes the text of the group into the buffer.
static bool append_group(TextBuffer* buffer, const char* text, size_t from, size_t to) {
    if (from == REGEX_NO_OFFSET || to == REGEX_NO_OFFSET) return true;
    if (to < from) return true;
    return buffer_append(buffer, text + from, to - from);
}

int regex_replace(Regex* regex, const char* text, size_t length,
                  const char* replacement, char** out, size_t* out_length) {
    if (!regex || !text || !replacement || !out) return -1;

    TextBuffer buffer;
    buffer_init(&buffer);

    size_t position = 0;
    while (position <= length) {
        size_t begin = 0;
        size_t end = 0;
        size_t offsets[REGEX_MAX_OFFSETS];
        for (int i = 0; i < REGEX_MAX_OFFSETS; i++) offsets[i] = REGEX_NO_OFFSET;
        if (!regex_search(regex, text, length, position, &begin, &end, NULL, offsets)) break;

        if (!buffer_append(&buffer, text + position, begin - position)) goto failed;

        // the replacement: $0, $1 .. $9, ${1} and $$
        const char* p = replacement;
        while (*p) {
            if (*p != '$') {
                if (!buffer_append(&buffer, p, 1)) goto failed;
                p++;
                continue;
            }
            const char* start = p;   // the dollar sign
            p++;
            if (*p == '$') {
                if (!buffer_append(&buffer, "$", 1)) goto failed;
                p++;
                continue;
            }
            bool braces = false;
            if (*p == '{') {
                braces = true;
                p++;
            }
            if (!isdigit((unsigned char)*p)) {
                // it is not a reference: the dollar sign is a literal
                if (!buffer_append(&buffer, start, (size_t)(p - start))) goto failed;
                continue;
            }
            int group = *p - '0';
            p++;
            if (braces) {
                if (*p != '}') {
                    if (!buffer_append(&buffer, start, (size_t)(p - start))) goto failed;
                    continue;
                }
                p++;
            }
            if (group > regex->group_count) {
                // there is no such group: the text is a literal
                if (!buffer_append(&buffer, start, (size_t)(p - start))) goto failed;
                continue;
            }
            if (!append_group(&buffer, text, offsets[2 * group], offsets[2 * group + 1])) {
                goto failed;
            }
        }

        if (end > begin) {
            position = end;
        } else {
            position = begin + 1;
        }
        if (position > length) position = length;
    }

    if (!buffer_append(&buffer, text + position, length - position)) goto failed;

    *out = buffer.data ? buffer.data : strdup("");
    if (!*out) return -1;
    if (out_length) *out_length = buffer.length;
    return 0;

failed:
    free(buffer.data);
    return -1;
}

int regex_split(Regex* regex, const char* text, size_t length,
                char*** out, size_t* out_count) {
    if (!regex || !text || !out) return -1;

    size_t capacity = 8;
    size_t count = 0;
    char** parts = (char**)malloc(sizeof(char*) * capacity);
    if (!parts) return -1;

    size_t position = 0;
    while (position <= length) {
        size_t begin = 0;
        size_t end = 0;
        if (!regex_search(regex, text, length, position, &begin, &end, NULL, NULL)) break;

        bool empty = (end == begin);
        if (empty && begin >= length) break;   // the empty match at the end
        size_t part_end = empty ? begin + 1 : begin;

        if (count == capacity) {
            size_t next = capacity * 2;
            char** grown = (char**)realloc(parts, sizeof(char*) * next);
            if (!grown) goto failed;
            parts = grown;
            capacity = next;
        }
        size_t size = part_end - position;
        char* part = (char*)malloc(size + 1);
        if (!part) goto failed;
        if (size) memcpy(part, text + position, size);
        part[size] = '\0';
        parts[count++] = part;

        if (!empty) {
            position = end;
        } else {
            position = begin + 1;
        }
        if (position > length) position = length;
    }

    // the last part
    if (count == capacity) {
        size_t next = capacity * 2;
        char** grown = (char**)realloc(parts, sizeof(char*) * next);
        if (!grown) goto failed;
        parts = grown;
        capacity = next;
    }
    size_t size = length - position;
    char* last = (char*)malloc(size + 1);
    if (!last) goto failed;
    if (size) memcpy(last, text + position, size);
    last[size] = '\0';
    parts[count++] = last;

    *out = parts;
    if (out_count) *out_count = count;
    return 0;

failed:
    for (size_t i = 0; i < count; i++) free(parts[i]);
    free(parts);
    return -1;
}
