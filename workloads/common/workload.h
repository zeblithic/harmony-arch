/* workload.h — Shared data generation and verification functions.
 *
 * Both the mutable and WORM workloads use identical fill and checksum
 * functions. Keeping them in a single header guarantees the experiment's
 * correctness invariant: both workloads produce the same checksums.
 */
#ifndef HARMONY_WORKLOAD_H
#define HARMONY_WORKLOAD_H

#include <stdint.h>
#include "config.h"

/* Deterministic data fill using a Linear Congruential Generator.
 * Keyed by (producer_id, block_index) for reproducible output. */
static inline void fill_block(uint8_t *buf, int producer_id, int block_index) {
    uint32_t seed = (uint32_t)(producer_id * BLOCKS_PER_PRODUCER + block_index);
    for (int i = 0; i < BLOCK_SIZE; i++) {
        seed = seed * 1103515245 + 12345;  /* LCG */
        buf[i] = (uint8_t)(seed >> 16);
    }
}

/* Simple checksum — sum of all bytes as uint32. */
static inline uint32_t checksum(const uint8_t *buf, int len) {
    uint32_t sum = 0;
    for (int i = 0; i < len; i++) {
        sum += buf[i];
    }
    return sum;
}

#endif /* HARMONY_WORKLOAD_H */
