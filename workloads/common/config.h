/* config.h — Shared constants for WORM coherence simulation workloads.
 *
 * Both the mutable and WORM workloads use these values so their results
 * are directly comparable. Change values here, not in individual workloads.
 */
#ifndef HARMONY_WORKLOAD_CONFIG_H
#define HARMONY_WORKLOAD_CONFIG_H

/* Number of producer threads (each paired with one consumer).
 * 2P + 2C + main = 5 threads → 5 cores in gem5 SE mode (1 context/core).
 * Main blocks on pthread_join, so 4 cores are actively generating traffic. */
#define NUM_PRODUCERS 2
#define NUM_CONSUMERS 2

/* Block size in bytes — matches Harmony's 4KB page size. */
#define BLOCK_SIZE 4096

/* Number of blocks each producer generates.
 * Kept at 32 so TOTAL_BLOCKS stays 64 (same data volume as 4P x 16B). */
#define BLOCKS_PER_PRODUCER 32

/* Total blocks across all producers. */
#define TOTAL_BLOCKS (NUM_PRODUCERS * BLOCKS_PER_PRODUCER)

/* Number of pre-allocated slots in the mutable workload's buffer pool.
 * Intentionally SMALLER than TOTAL_BLOCKS to force in-place slot reuse —
 * this is what generates the M→I invalidation traffic we want to measure.
 * Must be >= NUM_PRODUCERS so each producer can claim a slot at startup. */
#define MUTABLE_POOL_SIZE 16

_Static_assert(MUTABLE_POOL_SIZE >= NUM_PRODUCERS,
               "MUTABLE_POOL_SIZE must be >= NUM_PRODUCERS for startup parallelism");

_Static_assert(NUM_PRODUCERS == NUM_CONSUMERS,
               "Workloads assume 1:1 producer-consumer pairing; "
               "update mailbox indexing before changing this ratio");

#endif /* HARMONY_WORKLOAD_CONFIG_H */
