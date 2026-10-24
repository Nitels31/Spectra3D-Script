#include "internal/lexer.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* name;
    TokenType  type;
} Keyword;

static const Keyword KEYWORDS[] = {
    { "let", TK_LET },       { "var", TK_VAR },       { "def", TK_DEF },
    { "class", TK_CLASS },
    { "struct", TK_STRUCT },
    { "extends", TK_EXTENDS },
    { "switch", TK_SWITCH },
    { "case", TK_CASE },
    { "default", TK_DEFAULT },
    { "try", TK_TRY },
    { "catch", TK_CATCH },
    { "import", TK_IMPORT },
    { "as", TK_AS },
    { "if", TK_IF },         { "elif", TK_ELIF },     { "else", TK_ELSE },
    { "while", TK_WHILE },   { "for", TK_FOR },       { "in", TK_IN },
    { "break", TK_BREAK },   { "continue", TK_CONTINUE }, { "return", TK_RETURN },
    { "true", TK_TRUE },     { "false", TK_FALSE },   { "nil", TK_NIL },
    { "and", TK_AND },       { "or", TK_OR },         { "not", TK_NOT }
};

#define KEYWORD_COUNT (sizeof(KEYWORDS) / sizeof(KEYWORDS[0]))

static int is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_part(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

void lexer_init(Lexer* lexer, const char* source, Arena* arena) {
    lexer->source = source;
    lexer->current = source;
    lexer->line = 1;
    lexer->line_start = 0;
    lexer->arena = arena;
}

// The column of the current position, counted from the start of the line.
static int current_column(Lexer* lexer) {
    return (int)(lexer->current - lexer->source - lexer->line_start) + 1;
}

static Token make_token(Lexer* lexer, TokenType type) {
    Token token;
    token.type = type;
    token.start = lexer->source;
    token.length = 0;
    token.line = lexer->line;
    token.column = current_column(lexer);
    token.int_val = 0;
    token.float_val = 0.0;
    token.is_fstring = false;
    return token;
}

static Token token_from(Lexer* lexer, TokenType type, const char* start) {
    Token token = make_token(lexer, type);
    token.start = start;
    token.length = (int)(lexer->current - start);
    return token;
}

static void lexer_error(Lexer* lexer, const char* message) {
    fprintf(stderr, "sps: syntax error on line %d: %s\n", lexer->line, message);
    exit(1);
}

static void skip_whitespace(Lexer* lexer) {
    for (;;) {
        char c = *lexer->current;
        switch (c) {
            case ' ':
            case '\t':
            case '\r':
                lexer->current++;
                break;
            case '\n':
                lexer->line++;
                lexer->current++;
                lexer->line_start = (size_t)(lexer->current - lexer->source);
                break;
            case '\\':
                // the continuation of the line
                if (lexer->current[1] == '\n' || lexer->current[1] == '\r') {
                    lexer->current++;
                    if (*lexer->current == '\r') lexer->current++;
                    if (*lexer->current == '\n') lexer->current++;
                    lexer->line++;
                    continue;
                }
                break;
            case '/':
                if (lexer->current[1] == '/') {
                    while (*lexer->current && *lexer->current != '\n') lexer->current++;
                } else if (lexer->current[1] == '*') {
                    lexer->current += 2;
                    for (;;) {
                        if (!*lexer->current) lexer_error(lexer, "unterminated block comment");
                        if (lexer->current[0] == '*' && lexer->current[1] == '/') {
                            lexer->current += 2;
                            break;
                        }
                        if (*lexer->current == '\n') lexer->line++;
                        lexer->current++;
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static TokenType identifier_type(const char* start, int length) {
    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        if ((int)strlen(KEYWORDS[i].name) == length &&
            memcmp(KEYWORDS[i].name, start, (size_t)length) == 0) {
            return KEYWORDS[i].type;
        }
    }
    return TK_IDENT;
}

static Token scan_identifier(Lexer* lexer) {
    const char* start = lexer->current;
    while (is_ident_part(*lexer->current)) lexer->current++;
    Token token = token_from(lexer, identifier_type(start, (int)(lexer->current - start)), start);
    return token;
}

static Token scan_number(Lexer* lexer) {
    const char* start = lexer->current;
    bool is_float = false;

    if (lexer->current[0] == '0' && (lexer->current[1] == 'x' || lexer->current[1] == 'X')) {
        lexer->current += 2;
        while (isxdigit((unsigned char)*lexer->current)) lexer->current++;
        Token token = token_from(lexer, TK_INT, start);
        token.int_val = (int64_t)strtoll(start, NULL, 16);
        return token;
    }

    while (isdigit((unsigned char)*lexer->current)) lexer->current++;

    if (*lexer->current == '.' && isdigit((unsigned char)lexer->current[1])) {
        is_float = true;
        lexer->current++;
        while (isdigit((unsigned char)*lexer->current)) lexer->current++;
    }

    if (*lexer->current == 'e' || *lexer->current == 'E') {
        const char* save = lexer->current;
        lexer->current++;
        if (*lexer->current == '+' || *lexer->current == '-') lexer->current++;
        if (isdigit((unsigned char)*lexer->current)) {
            is_float = true;
            while (isdigit((unsigned char)*lexer->current)) lexer->current++;
        } else {
            lexer->current = save;
        }
    }

    Token token = token_from(lexer, is_float ? TK_FLOAT : TK_INT, start);
    if (is_float) {
        token.float_val = strtod(start, NULL);
    } else {
        char* end = NULL;
        long long value = strtoll(start, &end, 10);
        if (end == start) lexer_error(lexer, "invalid number");
        token.int_val = (int64_t)value;
    }
    return token;
}

// Processes the escape sequences of a string literal.
char* lexer_unescape(Arena* arena, const char* text, int length) {
    size_t capacity = (size_t)length + 1;
    char* buffer = (char*)malloc(capacity);
    if (!buffer) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    size_t out = 0;
    for (int i = 0; i < length; i++) {
        char c = text[i];
        if (c == '\\' && i + 1 < length) {
            char esc = text[++i];
            switch (esc) {
                case 'n':  c = '\n'; break;
                case 't':  c = '\t'; break;
                case 'r':  c = '\r'; break;
                case '0':  c = '\0'; break;
                case '\\': c = '\\'; break;
                case '"':  c = '"';  break;
                case '\'': c = '\''; break;
                default:
                    buffer[out++] = '\\';
                    c = esc;
                    break;
            }
        }
        buffer[out++] = c;
    }
    buffer[out] = '\0';
    char* result = arena_strndup(arena, buffer, out);
    free(buffer);
    return result;
}

static Token scan_string(Lexer* lexer, char quote) {
    lexer->current++; // skip the opening quote

    size_t capacity = 32;
    size_t length = 0;
    char* buffer = (char*)malloc(capacity);
    if (!buffer) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }

    for (;;) {
        char c = *lexer->current;
        if (c == '\0' || c == '\n') {
            free(buffer);
            lexer_error(lexer, "unterminated string literal");
        }
        if (c == quote) {
            lexer->current++;
            break;
        }
        if (c == '\\') {
            lexer->current++;
            char esc = *lexer->current++;
            char out;
            switch (esc) {
                case 'n':  out = '\n'; break;
                case 't':  out = '\t'; break;
                case 'r':  out = '\r'; break;
                case '0':  out = '\0'; break;
                case '\\': out = '\\'; break;
                case '"':  out = '"';  break;
                case '\'': out = '\''; break;
                default:
                    free(buffer);
                    lexer_error(lexer, "unknown escape sequence");
                    out = 0;
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
            buffer[length++] = out;
            continue;
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
        lexer->current++;
    }

    char* stored = arena_strndup(lexer->arena, buffer, length);
    free(buffer);

    Token token = make_token(lexer, TK_STRING);
    token.start = stored;
    token.length = (int)length;
    return token;
}

// A multi line string: """the text ... """
static Token scan_multiline_string(Lexer* lexer) {
    lexer->current += 3; // the opening quotes

    size_t capacity = 64;
    size_t length = 0;
    char* buffer = (char*)malloc(capacity);
    if (!buffer) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }

    for (;;) {
        if (lexer->current[0] == '\0') {
            free(buffer);
            lexer_error(lexer, "unterminated multi line string");
        }
        if (lexer->current[0] == '"' && lexer->current[1] == '"' && lexer->current[2] == '"') {
            lexer->current += 3;
            break;
        }
        char c = *lexer->current++;
        if (c == '\n') lexer->line++;
        if (c == '\\' && lexer->current[0] == '\n') {
            // the escape of the line inside a multi line string
            lexer->current++;
            lexer->line++;
            continue;
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

    char* stored = arena_strndup(lexer->arena, buffer, length);
    free(buffer);

    Token token = make_token(lexer, TK_STRING);
    token.start = stored;
    token.length = (int)length;
    return token;
}

// An f-string keeps its content raw: the parser splits it into the literal
// parts and the expressions.
static Token scan_fstring(Lexer* lexer, char quote) {
    lexer->current++; // skip the opening quote

    size_t capacity = 32;
    size_t length = 0;
    char* buffer = (char*)malloc(capacity);
    if (!buffer) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }

    int depth = 0;
    for (;;) {
        char c = *lexer->current;
        if (c == '\0' || c == '\n') {
            free(buffer);
            lexer_error(lexer, "unterminated f-string");
        }
        if (c == '\\') {
            if (length + 2 >= capacity) {
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
            lexer->current++;
            if (!*lexer->current) break;
            buffer[length++] = *lexer->current;
            lexer->current++;
            continue;
        }
        if (c == quote && depth == 0) {
            lexer->current++;
            break;
        }
        if (c == '{') depth++;
        if (c == '}') depth--;
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
        lexer->current++;
    }

    char* stored = arena_strndup(lexer->arena, buffer, length);
    free(buffer);

    Token token = make_token(lexer, TK_STRING);
    token.start = stored;
    token.length = (int)length;
    token.is_fstring = true;
    return token;
}

Token lexer_next(Lexer* lexer) {
    skip_whitespace(lexer);
    const char* start = lexer->current;

    if (*start == '\0') return token_from(lexer, TK_EOF, start);

    if (is_ident_start(*start)) {
        // an f-string: f"text {value}"
        if ((start[0] == 'f' || start[0] == 'F') && !is_ident_part(start[1]) &&
            (start[1] == '"' || start[1] == '\'')) {
            lexer->current++;
            return scan_fstring(lexer, start[1]);
        }
        return scan_identifier(lexer);
    }

    if (isdigit((unsigned char)*start)) return scan_number(lexer);

    if (start[0] == '"' && start[1] == '"' && start[2] == '"') return scan_multiline_string(lexer);
    if (*start == '"' || *start == '\'') return scan_string(lexer, *start);

    lexer->current++;
    switch (*start) {
        case '(': return token_from(lexer, TK_LPAREN, start);
        case ')': return token_from(lexer, TK_RPAREN, start);
        case '{': return token_from(lexer, TK_LBRACE, start);
        case '}': return token_from(lexer, TK_RBRACE, start);
        case '[': return token_from(lexer, TK_LBRACKET, start);
        case ']': return token_from(lexer, TK_RBRACKET, start);
        case ',': return token_from(lexer, TK_COMMA, start);
        case '.': return token_from(lexer, TK_DOT, start);
        case ';': return token_from(lexer, TK_SEMI, start);
        case ':': return token_from(lexer, TK_COLON, start);
        case '?': return token_from(lexer, TK_QUESTION, start);
        case '+':
            if (*lexer->current == '=') { lexer->current++; return token_from(lexer, TK_PLUS_EQ, start); }
            return token_from(lexer, TK_PLUS, start);
        case '-':
            if (*lexer->current == '=') { lexer->current++; return token_from(lexer, TK_MINUS_EQ, start); }
            return token_from(lexer, TK_MINUS, start);
        case '*':
            if (*lexer->current == '=') { lexer->current++; return token_from(lexer, TK_STAR_EQ, start); }
            return token_from(lexer, TK_STAR, start);
        case '/':
            if (*lexer->current == '=') { lexer->current++; return token_from(lexer, TK_SLASH_EQ, start); }
            return token_from(lexer, TK_SLASH, start);
        case '%':
            if (*lexer->current == '=') { lexer->current++; return token_from(lexer, TK_PERCENT_EQ, start); }
            return token_from(lexer, TK_PERCENT, start);
        case '=':
            if (*lexer->current == '=') { lexer->current++; return token_from(lexer, TK_EQ_EQ, start); }
            return token_from(lexer, TK_EQ, start);
        case '!':
            if (*lexer->current == '=') { lexer->current++; return token_from(lexer, TK_NE, start); }
            return token_from(lexer, TK_BANG, start);
        case '<':
            if (*lexer->current == '=') { lexer->current++; return token_from(lexer, TK_LE, start); }
            if (*lexer->current == '<') { lexer->current++; return token_from(lexer, TK_SHL, start); }
            return token_from(lexer, TK_LT, start);
        case '>':
            if (*lexer->current == '=') { lexer->current++; return token_from(lexer, TK_GE, start); }
            if (*lexer->current == '>') { lexer->current++; return token_from(lexer, TK_SHR, start); }
            return token_from(lexer, TK_GT, start);
        case '&':
            if (*lexer->current == '&') { lexer->current++; return token_from(lexer, TK_AND_AND, start); }
            return token_from(lexer, TK_AMP, start);
        case '|':
            if (*lexer->current == '|') { lexer->current++; return token_from(lexer, TK_OR_OR, start); }
            return token_from(lexer, TK_PIPE, start);
        case '^': return token_from(lexer, TK_CARET, start);
        case '~': return token_from(lexer, TK_TILDE, start);
        default: break;
    }

    char message[64];
    snprintf(message, sizeof(message), "unexpected character '%c'", *start);
    lexer_error(lexer, message);
    return token_from(lexer, TK_EOF, start);
}

const char* token_type_name(TokenType type) {
    switch (type) {
        case TK_EOF:      return "end of file";
        case TK_IDENT:    return "identifier";
        case TK_INT:      return "integer";
        case TK_FLOAT:    return "float";
        case TK_STRING:   return "string";
        case TK_LET:      return "'let'";
        case TK_VAR:      return "'var'";
        case TK_DEF:      return "'def'";
        case TK_CLASS:    return "'class'";
        case TK_STRUCT:   return "'struct'";
        case TK_EXTENDS:  return "'extends'";
        case TK_SWITCH:   return "'switch'";
        case TK_CASE:     return "'case'";
        case TK_DEFAULT:  return "'default'";
        case TK_TRY:      return "'try'";
        case TK_CATCH:    return "'catch'";
        case TK_IMPORT:   return "'import'";
        case TK_AS:      return "'as'";
        case TK_IF:       return "'if'";
        case TK_ELIF:     return "'elif'";
        case TK_ELSE:     return "'else'";
        case TK_WHILE:    return "'while'";
        case TK_FOR:      return "'for'";
        case TK_IN:       return "'in'";
        case TK_BREAK:    return "'break'";
        case TK_CONTINUE: return "'continue'";
        case TK_RETURN:   return "'return'";
        case TK_TRUE:     return "'true'";
        case TK_FALSE:    return "'false'";
        case TK_NIL:      return "'nil'";
        case TK_AND:      return "'and'";
        case TK_OR:       return "'or'";
        case TK_NOT:      return "'not'";
        case TK_LPAREN:   return "'('";
        case TK_RPAREN:   return "')'";
        case TK_LBRACE:   return "'{'";
        case TK_RBRACE:   return "'}'";
        case TK_LBRACKET: return "'['";
        case TK_RBRACKET: return "']'";
        case TK_COMMA:    return "','";
        case TK_DOT:      return "'.'";
        case TK_SEMI:     return "';'";
        case TK_COLON:    return "':'";
        case TK_QUESTION: return "'?'";
        case TK_PLUS:     return "'+'";
        case TK_MINUS:    return "'-'";
        case TK_STAR:     return "'*'";
        case TK_SLASH:    return "'/'";
        case TK_PERCENT:  return "'%'";
        case TK_EQ:       return "'='";
        case TK_EQ_EQ:    return "'=='";
        case TK_NE:       return "'!='";
        case TK_LT:       return "'<'";
        case TK_LE:       return "'<='";
        case TK_GT:       return "'>'";
        case TK_GE:       return "'>='";
        case TK_PLUS_EQ:  return "'+='";
        case TK_MINUS_EQ: return "'-='";
        case TK_STAR_EQ:  return "'*='";
        case TK_SLASH_EQ: return "'/='";
        case TK_PERCENT_EQ: return "'%='";
        case TK_AMP:      return "'&'";
        case TK_PIPE:     return "'|'";
        case TK_CARET:    return "'^'";
        case TK_TILDE:    return "'~'";
        case TK_SHL:      return "'<<'";
        case TK_SHR:      return "'>>'";
        case TK_AND_AND:  return "'&&'";
        case TK_OR_OR:    return "'||'";
        case TK_BANG:     return "'!'";
    }
    return "token";
}
