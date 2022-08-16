#ifndef ARENA_H
#define ARENA_H

typedef struct {
	unsigned char *buffer;
	size_t buffer_length;
	size_t previous_offset;
	size_t current_offset;
} Arena;

typedef struct {
	Arena *arena;
	size_t previous_offset;
	size_t current_offset;
} Arena_Checkpoint;

void *arena_alloc_align(Arena *arena, size_t size, size_t alignment);
void *arena_alloc(Arena *arena, size_t size);
void arena_init(Arena *arena, void *buffer, size_t buffer_length);
void arena_free(Arena *arena);

Arena_Checkpoint arena_create_checkpoint(Arena *arena);
void arena_restore(Arena_Checkpoint checkpoint);

#endif
