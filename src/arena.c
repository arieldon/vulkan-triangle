#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "debug.h"

#ifndef DEFAULT_ALIGNMENT
#	define DEFAULT_ALIGNMENT	(sizeof(void *) * 2)
#endif

static bool
is_power_of_two(uintptr_t x)
{
	return (x & (x - 1)) == 0;
}

static uintptr_t
align(uintptr_t pointer, size_t alignment)
{
	assert(is_power_of_two(alignment));

	uintptr_t p = pointer;
	uintptr_t a = alignment;
	uintptr_t m = p & (a - 1);

	// Push the address to next aligned value.
	if (m != 0) p += a - m;

	return p;
}

void *
arena_alloc_align(Arena *arena, size_t size, size_t alignment)
{
	// Align current offset to the specified alignment by pushing it forward if
	// necessary.
	uintptr_t current_pointer = (uintptr_t)arena->buffer + (uintptr_t)arena->current_offset;
	uintptr_t offset = align(current_pointer, alignment);
	offset -= (uintptr_t)arena->buffer;

	// Ensure remaining capacity exists in backing buffer.
	if (offset + size <= arena->buffer_length) {
		void *p = &arena->buffer[offset];
		arena->previous_offset = offset;
		arena->current_offset = offset + size;

		memset(p, 0, size);
		return p;
	}

	return NULL;
}

void *
arena_alloc(Arena *arena, size_t size)
{
	return arena_alloc_align(arena, size, DEFAULT_ALIGNMENT);
}

void
arena_init(Arena *arena, void *buffer, size_t buffer_length)
{
	arena->buffer = (unsigned char *)buffer;
	arena->buffer_length = buffer_length;
	arena->current_offset = 0;
	arena->previous_offset = 0;
}

void
arena_free(Arena *arena)
{
	arena->current_offset = 0;
	arena->previous_offset = 0;
}

Arena_Checkpoint
arena_create_checkpoint(Arena *arena)
{
	return (Arena_Checkpoint){
		.arena = arena,
		.previous_offset = arena->previous_offset,
		.current_offset = arena->current_offset,
	};
}

void
arena_restore(Arena_Checkpoint checkpoint)
{
	checkpoint.arena->previous_offset = checkpoint.previous_offset;
	checkpoint.arena->current_offset = checkpoint.current_offset;
}
