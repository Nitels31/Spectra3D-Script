// Spectra3D Script - parser (recursive descent)
#ifndef SPS_PARSER_H
#define SPS_PARSER_H

#include "internal/ast.h"
#include "internal/arena.h"

#include <setjmp.h>

// The host can collect the errors of the parser.
typedef void (*SpsParseErrorHandler)(void* user, int line, const char* message);

typedef struct {
    Lexer   lexer;
    Arena*  arena;
    Token   current;
    Token   previous;
    bool    had_error;
    jmp_buf error_jump;
    SpsParseErrorHandler on_error;
    void*   error_user;
    char    file[512];   // the name of the file for the diagnostics
} Parser;

// Returns NULL if a syntax error was found.
Node* parser_parse(Parser* parser, Arena* arena, const char* source);

#endif /* SPS_PARSER_H */
