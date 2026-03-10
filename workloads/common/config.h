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

/* Number of pre-allocated slots in the mutable workload's buffer pool. */
#define MUTABLE_POOL_SIZE 64

#endif /* HARMONY_WORKLOAD_CONFIG_H */
