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
    /* No memset — producers overwrite each region entirely via fill_block()
     * before any consumer reads it.  Zeroing here would place the entire
     * arena into M-state on core 0, inflating M→I transitions when
     * producer threads on other cores claim their regions. */
    return 0;
}

/* Atomically claim `size` bytes from the arena.
 * Returns the offset of the allocated region, or (size_t)-1 on overflow.
 * Uses CAS loop to avoid permanently corrupting the offset on overflow.
 * Release ordering on success so callers with a matching acquire can
 * safely access the claimed region without an additional fence. */
static inline size_t arena_alloc(arena_t *a, size_t size) {
    size_t old_off, new_off;
    do {
        old_off = atomic_load_explicit(&a->offset, memory_order_relaxed);
        if (old_off + size > a->capacity) return (size_t)-1;
        new_off = old_off + size;
    } while (!atomic_compare_exchange_weak_explicit(
                 &a->offset, &old_off, new_off,
                 memory_order_release, memory_order_relaxed));
    return old_off;
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
