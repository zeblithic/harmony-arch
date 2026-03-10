/* arena.h — Append-only arena allocator for WORM workloads.
 *
 * The arena is a contiguous region of memory with a monotonically
 * increasing offset. Each allocation claims the next N bytes atomically.
 * Memory is never freed or reused — this is the software expression of
 * WORM (Write-Once-Read-Many) semantics.
 */
#ifndef HARMONY_ARENA_H
#define HARMONY_ARENA_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *base;
    size_t capacity;
    atomic_size_t offset;  /* monotonically increasing */
} arena_t;

/* Initialize an arena with the given capacity. Returns 0 on success. */
static inline int arena_init(arena_t *a, size_t capacity) {
    a->base = (uint8_t *)malloc(capacity);
    if (!a->base) return -1;
    a->capacity = capacity;
    atomic_store(&a->offset, 0);
    memset(a->base, 0, capacity);
    return 0;
}

/* Atomically claim `size` bytes from the arena.
 * Returns the offset of the allocated region, or (size_t)-1 on overflow. */
static inline size_t arena_alloc(arena_t *a, size_t size) {
    size_t off = atomic_fetch_add(&a->offset, size);
    if (off + size > a->capacity) return (size_t)-1;
    return off;
}

/* Get a pointer to the data at the given offset. */
static inline uint8_t *arena_ptr(arena_t *a, size_t offset) {
    return a->base + offset;
}

/* Free the arena's backing memory. */
static inline void arena_destroy(arena_t *a) {
    free(a->base);
    a->base = NULL;
}

#endif /* HARMONY_ARENA_H */
