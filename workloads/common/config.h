/* config.h — Shared constants for WORM coherence simulation workloads.
 *
 * Both the mutable and WORM workloads use these values so their results
 * are directly comparable. Change values here, not in individual workloads.
 */
#ifndef HARMONY_WORKLOAD_CONFIG_H
#define HARMONY_WORKLOAD_CONFIG_H

/* Number of producer threads (each paired with one consumer). */
#define NUM_PRODUCERS 4
#define NUM_CONSUMERS 4

/* Block size in bytes — matches Harmony's 4KB page size. */
#define BLOCK_SIZE 4096

/* Number of blocks each producer generates. */
#define BLOCKS_PER_PRODUCER 16

/* Total blocks across all producers. */
#define TOTAL_BLOCKS (NUM_PRODUCERS * BLOCKS_PER_PRODUCER)

/* Number of pre-allocated slots in the mutable workload's buffer pool.
 * Intentionally SMALLER than TOTAL_BLOCKS to force in-place slot reuse —
 * this is what generates the M→I invalidation traffic we want to measure.
 * Must be >= NUM_PRODUCERS so each producer can claim a slot at startup. */
#define MUTABLE_POOL_SIZE 16

_Static_assert(MUTABLE_POOL_SIZE >= NUM_PRODUCERS,
               "MUTABLE_POOL_SIZE must be >= NUM_PRODUCERS for startup parallelism");

#endif /* HARMONY_WORKLOAD_CONFIG_H */
