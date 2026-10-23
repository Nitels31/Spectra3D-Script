// Spectra3D Script - bytecode compiler (AST -> bytecode)
#ifndef SPS_COMPILER_H
#define SPS_COMPILER_H

#include "ast.h"
#include "program.h"
#include "vm.h"

#include <stdbool.h>

// The host can collect the errors of the compiler.
typedef void (*SpsCompileErrorHandler)(void* user, int line, const char* message);

// Compiles the AST into the program. Returns false and prints a diagnostic
// when the source code contains an error.
bool compiler_compile(Program* program, VM* vm, Node* ast, const char* file_name);

// The same, but the errors are reported to the host instead of the console.
bool compiler_compile_ex(Program* program, VM* vm, Node* ast, const char* file_name,
                         SpsCompileErrorHandler on_error, void* user);

// The same, but the errors are printed in the standard format
// "file:line:column: error: message".
bool compiler_compile_diagnostic(Program* program, VM* vm, Node* ast, const char* file_name);

// The folders where the imported files (the packages of the project) are searched.
void compiler_set_search_paths(const char* const* paths, int count);

#endif /* SPS_COMPILER_H */
