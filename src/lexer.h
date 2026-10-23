// Spectra3D Script - lexer (.sps)
#ifndef SPS_LEXER_H
#define SPS_LEXER_H

#include "arena.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TK_EOF,
    TK_IDENT,
    TK_INT,
    TK_FLOAT,
    TK_STRING,

    // keywords
    TK_LET,
    TK_VAR,
    TK_DEF,
    TK_CLASS,
    TK_STRUCT,
    TK_EXTENDS,
    TK_SWITCH,
    TK_CASE,
    TK_DEFAULT,
    TK_TRY,
    TK_CATCH,
    TK_IMPORT,
    TK_AS,
    TK_IF,
    TK_ELIF,
    TK_ELSE,
    TK_WHILE,
    TK_FOR,
    TK_IN,
    TK_BREAK,
    TK_CONTINUE,
    TK_RETURN,
    TK_TRUE,
    TK_FALSE,
    TK_NIL,
    TK_AND,
    TK_OR,
    TK_NOT,

    // punctuation
    TK_LPAREN,
    TK_RPAREN,
    TK_LBRACE,
    TK_RBRACE,
    TK_LBRACKET,
    TK_RBRACKET,
    TK_COMMA,
    TK_DOT,
    TK_SEMI,
    TK_COLON,
    TK_QUESTION,

    // operators
    TK_PLUS,
    TK_MINUS,
    TK_STAR,
    TK_SLASH,
    TK_PERCENT,
    TK_EQ,
    TK_EQ_EQ,
    TK_NE,
    TK_LT,
    TK_LE,
    TK_GT,
    TK_GE,
    TK_PLUS_EQ,
    TK_MINUS_EQ,
    TK_STAR_EQ,
    TK_SLASH_EQ,
    TK_PERCENT_EQ,
    TK_AMP,
    TK_PIPE,
    TK_CARET,
    TK_TILDE,
    TK_SHL,
    TK_SHR,
    TK_AND_AND,
    TK_OR_OR,
    TK_BANG
} TokenType;

typedef struct {
    TokenType   type;
    const char* start;
    int         length;
    int         line;
    int         column;
    int64_t     int_val;
    double      float_val;
    bool        is_fstring; // an f-string with {expressions}
} Token;

typedef struct {
    const char* source;
    const char* current;
    int         line;
    size_t      line_start; // the position of the beginning of the line
    Arena*      arena;
} Lexer;

void  lexer_init(Lexer* lexer, const char* source, Arena* arena);
Token lexer_next(Lexer* lexer);
const char* token_type_name(TokenType type);

// Processes the escape sequences of a string literal.
char* lexer_unescape(Arena* arena, const char* text, int length);

#endif /* SPS_LEXER_H */
