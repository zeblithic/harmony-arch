/* Mutable producer-consumer workload for WORM coherence simulation.
 *
 * This version uses traditional in-place mutation:
 * - Shared buffer pool with locks (spinlocks via atomics)
 * - Producers overwrite existing slots
 * - Status array bounces between cores
 *
 * This triggers the full MESI cache coherence protocol.
 */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/config.h"
#include "../common/workload.h"

/* Slot states */
#define SLOT_FREE  0
#define SLOT_READY 1

/* Shared buffer pool — pre-allocated, reused (mutated) */
static uint8_t buffer_pool[MUTABLE_POOL_SIZE][BLOCK_SIZE];
static atomic_int slot_status[MUTABLE_POOL_SIZE];
static atomic_int slot_lock[MUTABLE_POOL_SIZE];

/* Next slot index for round-robin assignment */
static atomic_int next_slot = 0;

/* Per-thread checksum results */
static uint32_t producer_checksums[NUM_PRODUCERS];
static uint32_t consumer_checksums[NUM_CONSUMERS];

/* Simple spinlock on slot */
static void lock_slot(int slot) {
    int expected = 0;
    while (!atomic_compare_exchange_weak(&slot_lock[slot], &expected, 1)) {
        expected = 0;
    }
}

static void unlock_slot(int slot) {
    atomic_store(&slot_lock[slot], 0);
}

/* Mailbox: producer writes (slot_index, block_index) pairs for its consumer.
 * We use a simple ring buffer per producer-consumer pair. */
typedef struct {
    atomic_int slot;     /* Which slot holds the data */
    atomic_int ready;    /* 1 when producer has written this entry */
} mailbox_entry_t;

static mailbox_entry_t mailboxes[NUM_PRODUCERS][BLOCKS_PER_PRODUCER];

static void *producer_thread(void *arg) {
    int id = (int)(intptr_t)arg;
    uint32_t local_checksum = 0;

    for (int b = 0; b < BLOCKS_PER_PRODUCER; b++) {
        /* Claim a slot (round-robin) */
        int slot = atomic_fetch_add(&next_slot, 1) % MUTABLE_POOL_SIZE;

        /* Wait for slot to be free, then lock it */
        while (1) {
            lock_slot(slot);
            if (atomic_load(&slot_status[slot]) == SLOT_FREE) {
                break;
            }
            unlock_slot(slot);
            /* Spin — in a real system we'd yield */
        }

        /* Write data INTO the slot (overwriting previous contents) */
        fill_block(buffer_pool[slot], id, b);

        /* Compute producer-side checksum */
        local_checksum += checksum(buffer_pool[slot], BLOCK_SIZE);

        /* Mark slot ready */
        atomic_store(&slot_status[slot], SLOT_READY);
        unlock_slot(slot);

        /* Tell our consumer which slot to read */
        atomic_store(&mailboxes[id][b].slot, slot);
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

        int slot = atomic_load(&mailboxes[id][b].slot);

        /* Invariant: producer stores ready=1 only after slot_status==SLOT_READY
         * and unlock_slot, so observing ready==1 (seq_cst) guarantees the slot
         * is SLOT_READY.  Use assert (compiles away under -DNDEBUG) to avoid
         * an always-present atomic_load that would generate a spurious
         * coherence event in the simulation, biasing M→I / invalidation counts
         * against the mutable baseline. */
        assert(atomic_load(&slot_status[slot]) == SLOT_READY);
        local_checksum += checksum(buffer_pool[slot], BLOCK_SIZE);

        /* Acquire lock only to update slot_status atomically. */
        lock_slot(slot);
        atomic_store(&slot_status[slot], SLOT_FREE);
        unlock_slot(slot);
    }

    consumer_checksums[id] = local_checksum;
    return NULL;
}

int main(void) {
    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumers[NUM_CONSUMERS];

    /* Initialize */
    memset(buffer_pool, 0, sizeof(buffer_pool));
    for (int i = 0; i < MUTABLE_POOL_SIZE; i++) {
        atomic_store(&slot_status[i], SLOT_FREE);
        atomic_store(&slot_lock[i], 0);
    }
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        for (int b = 0; b < BLOCKS_PER_PRODUCER; b++) {
            atomic_store(&mailboxes[i][b].ready, 0);
        }
    }

    printf("Mutable producer-consumer: %d producers, %d consumers, "
           "%d blocks of %d bytes\n",
           NUM_PRODUCERS, NUM_CONSUMERS, TOTAL_BLOCKS, BLOCK_SIZE);

    /* Launch consumers first (they spin-wait) */
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        if (pthread_create(&consumers[i], NULL, consumer_thread, (void *)(intptr_t)i) != 0) {
            perror("pthread_create consumer");
            return 1;
        }
    }

    /* Launch producers */
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        if (pthread_create(&producers[i], NULL, producer_thread, (void *)(intptr_t)i) != 0) {
            perror("pthread_create producer");
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

    /* Verify checksums */
    uint32_t total_producer = 0, total_consumer = 0;
    for (int i = 0; i < NUM_PRODUCERS; i++) total_producer += producer_checksums[i];
    for (int i = 0; i < NUM_CONSUMERS; i++) total_consumer += consumer_checksums[i];

    printf("Producer checksum: %u\n", total_producer);
    printf("Consumer checksum: %u\n", total_consumer);

    if (total_producer == total_consumer) {
        printf("PASS: Checksums match.\n");
    } else {
        printf("FAIL: Checksum mismatch!\n");
        return 1;
    }

    return 0;
}
