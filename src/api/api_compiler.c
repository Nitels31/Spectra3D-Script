// Spectra3D Script - the compiler of the public API

#include "api_internal.h"
#include "internal/compiler.h"
#include "internal/parser.h"

#include <stdio.h>

// The diagnostics of the last compilation.
static void add_diagnostic(SpsContext* context, int line, const char* message) {
    if (context->diagnostic_count == context->diagnostic_cap) {
        int capacity = context->diagnostic_cap ? context->diagnostic_cap * 2 : 8;
        context->diagnostics = (SpsDiagnostic*)realloc(context->diagnostics,
                                                      sizeof(SpsDiagnostic) * (size_t)capacity);
        if (!context->diagnostics) return;
        context->diagnostic_cap = capacity;
    }

    SpsDiagnostic* diagnostic = &context->diagnostics[context->diagnostic_count++];
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->line = line;
    snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message ? message : "");
}

// The error of the parser.
static void parse_error_handler(void* user, int line, const char* message) {
    SpsContext* context = (SpsContext*)user;
    add_diagnostic(context, line, message);
    sps_internal_set_error(context, message);
}

// The error of the compiler.
static void compile_error_handler(void* user, int line, const char* message) {
    SpsContext* context = (SpsContext*)user;
    add_diagnostic(context, line, message);
    sps_internal_set_error(context, message);
}

static SpsChunk* create_chunk(SpsContext* context, const char* name) {
    SpsChunk* chunk = (SpsChunk*)calloc(1, sizeof(SpsChunk));
    if (!chunk) return NULL;

    program_init(&chunk->program);
    chunk->context = context;
    chunk->name = (char*)malloc(strlen(name ? name : "<chunk>") + 1);
    if (!chunk->name) {
        free(chunk);
        return NULL;
    }
    strcpy(chunk->name, name ? name : "<chunk>");
    return chunk;
}

static void keep_chunk(SpsContext* context, SpsChunk* chunk) {
    if (context->chunk_count == context->chunk_cap) {
        int capacity = context->chunk_cap ? context->chunk_cap * 2 : 8;
        context->chunks = (SpsChunk**)realloc(context->chunks, sizeof(SpsChunk*) * (size_t)capacity);
        if (!context->chunks) return;
        context->chunk_cap = capacity;
    }
    context->chunks[context->chunk_count++] = chunk;
}

SpsChunk* sps_compile(SpsContext* context, const char* source, const char* chunk_name) {
    if (!context || !source) return NULL;

    context->diagnostic_count = 0;
    context->last_error[0] = '\0';

    SpsChunk* chunk = create_chunk(context, chunk_name);
    if (!chunk) {
        sps_internal_set_error(context, "out of memory");
        return NULL;
    }

    Parser parser;
    memset(&parser, 0, sizeof(parser));
    parser.on_error = parse_error_handler;
    parser.error_user = context;

    Node* ast = parser_parse(&parser, &chunk->program.arena, source);
    if (!ast) {
        program_release(&chunk->program);
        free(chunk->name);
        free(chunk);
        return NULL;
    }

    if (!compiler_compile_ex(&chunk->program, context->vm, ast, chunk->name,
                             compile_error_handler, context)) {
        program_release(&chunk->program);
        free(chunk->name);
        free(chunk);
        return NULL;
    }

    keep_chunk(context, chunk);
    return chunk;
}

SpsChunk* sps_compile_file(SpsContext* context, const char* path) {
    if (!context || !path) return NULL;

    FILE* file = fopen(path, "rb");
    if (!file) {
        sps_internal_set_error(context, "cannot open the file");
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) size = 0;
    char* source = (char*)malloc((size_t)size + 1);
    if (!source) {
        fclose(file);
        sps_internal_set_error(context, "out of memory");
        return NULL;
    }
    size_t read = fread(source, 1, (size_t)size, file);
    fclose(file);
    source[read] = '\0';

    SpsChunk* chunk = sps_compile(context, source, path);
    free(source);
    return chunk;
}

void sps_chunk_free(SpsContext* context, SpsChunk* chunk) {
    if (!chunk) return;

    for (int i = 0; i < context->chunk_count; i++) {
        if (context->chunks[i] == chunk) {
            for (int j = i; j + 1 < context->chunk_count; j++) {
                context->chunks[j] = context->chunks[j + 1];
            }
            context->chunk_count--;
            break;
        }
    }

    program_release(&chunk->program);
    free(chunk->name);
    free(chunk);
}

const SpsDiagnostic* sps_compiler_diagnostics(SpsContext* context, int* count) {
    if (count) *count = context ? context->diagnostic_count : 0;
    return context ? context->diagnostics : NULL;
}

bool sps_chunk_write(SpsContext* context, SpsChunk* chunk, const char* path) {
    if (!context || !chunk || !path) return false;
    return program_serialize(&chunk->program, path);
}
