/* WORM producer-consumer workload for cache coherence simulation.
 *
 * This version uses Write-Once-Read-Many (WORM) memory semantics:
 * - Append-only arena with atomic offset (no overwrites)
 * - Each cache line transitions I→S/E exactly once, stays valid forever
 * - Single-writer mailboxes (no coherence contention on coordination)
 * - No locks
 *
 * This minimizes MESI coherence traffic: no M→I, no invalidation broadcasts.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/arena.h"
#include "../common/config.h"
#include "../common/workload.h"

/* Shared arena — append-only, never freed */
static arena_t arena;

/* Per-producer mailbox: producer writes offset, consumer reads it.
 * Single-writer/single-reader — no coherence contention. */
typedef struct {
    atomic_size_t offset;  /* Arena offset of the block */
    atomic_int ready;      /* 1 when producer has written this entry */
} mailbox_entry_t;

static mailbox_entry_t mailboxes[NUM_PRODUCERS][BLOCKS_PER_PRODUCER];

/* Per-thread checksum results */
static uint32_t producer_checksums[NUM_PRODUCERS];
static uint32_t consumer_checksums[NUM_CONSUMERS];

static void *producer_thread(void *arg) {
    int id = (int)(intptr_t)arg;
    uint32_t local_checksum = 0;

    for (int b = 0; b < BLOCKS_PER_PRODUCER; b++) {
        /* Claim fresh region from arena — never previously written */
        size_t off = arena_alloc(&arena, BLOCK_SIZE);
        if (off == (size_t)-1) {
            fprintf(stderr, "Arena overflow!\n");
            exit(1);
        }

        /* Write data to the NEW region (write-once) */
        uint8_t *block = arena_ptr(&arena, off);
        fill_block(block, id, b);

        /* Compute producer-side checksum */
        local_checksum += checksum(block, BLOCK_SIZE);

        /* Publish offset to mailbox (single-writer) */
        atomic_store(&mailboxes[id][b].offset, off);
        atomic_store(&mailboxes[id][b].ready, 1);
    }

    producer_checksums[id] = local_checksum;
    return NULL;
}

static void *consumer_thread(void *arg) {
    int id = (int)(intptr_t)arg;
    uint32_t local_checksum = 0;

    for (int b = 0; b < BLOCKS_PER_PRODUCER; b++) {
        /* Wait for our producer to post */
        while (!atomic_load(&mailboxes[id][b].ready)) {
            /* Spin */
        }

        /* Read offset and data (immutable — will never change) */
        size_t off = atomic_load(&mailboxes[id][b].offset);
        const uint8_t *block = arena_ptr(&arena, off);

        /* Compute checksum */
        local_checksum += checksum(block, BLOCK_SIZE);

        /* No slot-free needed — WORM data is never reclaimed */
    }

    consumer_checksums[id] = local_checksum;
    return NULL;
}

int main(void) {
    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumers[NUM_CONSUMERS];

    /* Initialize arena — sized for all blocks */
    size_t arena_size = (size_t)TOTAL_BLOCKS * BLOCK_SIZE;
    if (arena_init(&arena, arena_size) != 0) {
        fprintf(stderr, "Failed to initialize arena\n");
        return 1;
    }

    /* Initialize mailboxes */
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        for (int b = 0; b < BLOCKS_PER_PRODUCER; b++) {
            atomic_store(&mailboxes[i][b].ready, 0);
        }
    }

    printf("WORM producer-consumer: %d producers, %d consumers, "
           "%d blocks of %d bytes\n",
           NUM_PRODUCERS, NUM_CONSUMERS, TOTAL_BLOCKS, BLOCK_SIZE);

    /* Launch consumers first (they spin-wait) */
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        if (pthread_create(&consumers[i], NULL, consumer_thread, (void *)(intptr_t)i) != 0) {
            perror("pthread_create consumer");
            arena_destroy(&arena);
            return 1;
        }
    }

    /* Launch producers */
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        if (pthread_create(&producers[i], NULL, producer_thread, (void *)(intptr_t)i) != 0) {
            perror("pthread_create producer");
            arena_destroy(&arena);
            return 1;
        }
    }

    /* Wait for all threads */
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_join(consumers[i], NULL);
    }

    /* Verify checksums match the mutable version */
    uint32_t total_producer = 0, total_consumer = 0;
    for (int i = 0; i < NUM_PRODUCERS; i++) total_producer += producer_checksums[i];
    for (int i = 0; i < NUM_CONSUMERS; i++) total_consumer += consumer_checksums[i];

    printf("Producer checksum: %u\n", total_producer);
    printf("Consumer checksum: %u\n", total_consumer);

    if (total_producer == total_consumer) {
        printf("PASS: Checksums match.\n");
    } else {
        printf("FAIL: Checksum mismatch!\n");
        arena_destroy(&arena);
        return 1;
    }

    arena_destroy(&arena);
    return 0;
}
