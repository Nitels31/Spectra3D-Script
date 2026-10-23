#include "arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_DEFAULT_BLOCK (64u * 1024u)
#define ARENA_ALIGN         16u

struct ArenaBlock {
    ArenaBlock* next;
    size_t used;
    size_t capacity;
    char*  data;
};

static void arena_out_of_memory(void) {
    fprintf(stderr, "sps: out of memory\n");
    exit(1);
}

void arena_init(Arena* arena) {
    arena->head = NULL;
    arena->total_bytes = 0;
    arena->block_size = ARENA_DEFAULT_BLOCK;
}

static ArenaBlock* arena_new_block(size_t size) {
    ArenaBlock* block = (ArenaBlock*)malloc(sizeof(ArenaBlock));
    if (!block) arena_out_of_memory();
    block->data = (char*)malloc(size);
    if (!block->data) {
        free(block);
        arena_out_of_memory();
    }
    block->next = NULL;
    block->used = 0;
    block->capacity = size;
    return block;
}

void* arena_alloc(Arena* arena, size_t size) {
    if (size == 0) size = 1;
    size = (size + (ARENA_ALIGN - 1)) & ~((size_t)ARENA_ALIGN - 1);

    if (!arena->head || arena->head->capacity - arena->head->used < size) {
        size_t block_size = arena->block_size;
        if (block_size < size) block_size = size;
        ArenaBlock* block = arena_new_block(block_size);
        block->next = arena->head;
        arena->head = block;
    }

    char* ptr = arena->head->data + arena->head->used;
    arena->head->used += size;
    arena->total_bytes += size;
    return ptr;
}

void* arena_calloc(Arena* arena, size_t count, size_t size) {
    if (count != 0 && size > (size_t)-1 / count) arena_out_of_memory();
    size_t total = count * size;
    void* ptr = arena_alloc(arena, total);
    memset(ptr, 0, total);
    return ptr;
}

char* arena_strndup(Arena* arena, const char* src, size_t len) {
    char* copy = (char*)arena_alloc(arena, len + 1);
    if (len) memcpy(copy, src, len);
    copy[len] = '\0';
    return copy;
}

char* arena_strdup(Arena* arena, const char* src) {
    return arena_strndup(arena, src, strlen(src));
}

void arena_release(Arena* arena) {
    ArenaBlock* block = arena->head;
    while (block) {
        ArenaBlock* next = block->next;
        free(block->data);
        free(block);
        block = next;
    }
    arena->head = NULL;
    arena->total_bytes = 0;
}

size_t arena_bytes_used(const Arena* arena) {
    return arena->total_bytes;
}
