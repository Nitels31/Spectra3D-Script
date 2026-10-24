// Spectra3D Script - arena allocator
//
// All compiler-owned memory is allocated from an arena.
// The arena is released with a single arena_release() call -> zero leaks.
#ifndef SPS_ARENA_H
#define SPS_ARENA_H

#include <stddef.h>

typedef struct ArenaBlock ArenaBlock;

typedef struct {
    ArenaBlock* head;
    size_t total_bytes;
    size_t block_size;
} Arena;

void  arena_init(Arena* arena);
void* arena_alloc(Arena* arena, size_t size);
void* arena_calloc(Arena* arena, size_t count, size_t size);
char* arena_strndup(Arena* arena, const char* src, size_t len);
char* arena_strdup(Arena* arena, const char* src);
void  arena_release(Arena* arena);
size_t arena_bytes_used(const Arena* arena);

#endif /* SPS_ARENA_H */
