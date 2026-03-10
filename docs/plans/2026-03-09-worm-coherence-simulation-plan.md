# WORM Cache Coherence Simulation — Implementation Plan (Phase A)

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a gem5-based simulation comparing cache coherence traffic between traditional mutable memory and WORM (Write-Once-Read-Many) memory patterns on a 4-core RISC-V system.

**Architecture:** Two C workloads (mutable vs. WORM producer-consumer) run on a gem5-simulated 4-core RISC-V system with Ruby's MESI_Two_Level cache coherence protocol. A Python comparison script extracts and compares coherence metrics from gem5's stats output.

**Tech Stack:** gem5 v25.1.0.0, C (cross-compiled with `riscv64-linux-gnu-gcc`), Python 3, RISC-V RV64GC ISA, Ruby memory system, Garnet2.0 interconnect.

**Reference:** `docs/plans/2026-03-09-worm-coherence-simulation-design.md`

---

## Important Notes

### Toolchain Correction

The design doc mentions `riscv64-unknown-elf-gcc` (bare-metal newlib). This is **wrong** for gem5 SE mode. gem5's Syscall Emulation mode emulates **Linux** syscalls (`sys_write`, `sys_mmap`, `sys_exit_group`, etc.), so the workloads must be compiled with a Linux-targeting toolchain:

- **Correct:** `riscv64-linux-gnu-gcc -static` (Linux ABI, static linking)
- **Wrong:** `riscv64-unknown-elf-gcc` (bare-metal newlib — different syscall conventions)

Static linking is mandatory because gem5 SE mode doesn't support dynamic linking for cross-architecture binaries.

### Platform Considerations

- **gem5 builds natively on Linux.** macOS can build gem5 itself but cross-compiling RISC-V Linux binaries requires a Linux environment.
- **Recommended setup:** Use the i9/4090 Linux machine (or a Docker container with Ubuntu 22.04+) for the full toolchain: gem5 build + RISC-V cross-compile + simulation runs.
- **If developing on macOS:** Edit files locally, push to git, build and run on the Linux machine.

### gem5 Build System (v25.x)

gem5 v25.x uses a new three-step build configuration:

```bash
# 1. Create build directory with base ISA config
scons defconfig build/RISCV build_opts/RISCV

# 2. Set Ruby protocol
scons setconfig build/RISCV PROTOCOL=MESI_Two_Level

# 3. Build
scons build/RISCV/gem5.opt -j$(nproc)
```

There is no pre-built `build_opts/RISCV_MESI_Two_Level` — the protocol is set separately.

### gem5 Config API

gem5 v25.x has **two** config APIs:

- **Legacy:** `configs/deprecated/example/se.py` — the old `se.py` is deprecated but still works
- **Modern:** gem5 stdlib (Python classes: `Simulator`, `Board`, `Processor`, `CacheHierarchy`) — cleaner, more explicit

This plan uses the **modern stdlib API** for the config script.

### Ruby Stats Format

Ruby stats in gem5 use this naming pattern:

```
board.cache_hierarchy.ruby_system.L1Cache_Controller.L1Cache_Controller-0.I_to_M
board.cache_hierarchy.ruby_system.L1Cache_Controller.L1Cache_Controller-0.M_to_I
board.cache_hierarchy.ruby_system.L1Cache_Controller.L1Cache_Controller-0.S_to_I
board.cache_hierarchy.ruby_system.network.msg_count.Request_Control
board.cache_hierarchy.ruby_system.network.msg_count.Response_Data
```

The exact naming depends on the cache hierarchy configuration. The comparison script will need to grep for patterns rather than exact keys.

---

## Task 1: Repository Scaffolding

**Files:**
- Create: `.gitignore`
- Create: `.gitmodules` (via `git submodule add`)
- Create: `README.md`
- Create: `workloads/common/` (directory)
- Create: `workloads/mutable/` (directory)
- Create: `workloads/worm/` (directory)
- Create: `configs/` (directory)
- Create: `scripts/` (directory)
- Create: `results/` (directory, gitignored)
- Create: `analysis/` (directory)

**Step 1: Create the `.gitignore`**

```gitignore
# gem5 build artifacts
gem5/build/

# Simulation results (large, regenerable)
results/

# Compiled workload binaries
workloads/mutable/mutable_prodcon
workloads/worm/worm_prodcon
workloads/mutable/*.o
workloads/worm/*.o

# Python
__pycache__/
*.pyc

# Editor
*.swp
*.swo
*~
.vscode/
.idea/

# macOS
.DS_Store
```

**Step 2: Create the directory structure**

```bash
mkdir -p workloads/common workloads/mutable workloads/worm configs scripts results analysis
```

**Step 3: Create a minimal `README.md`**

```markdown
# harmony-arch

Microarchitectural simulation experiments for the Harmony protocol stack.

## Phase A: WORM Cache Coherence

Measures the cache coherence traffic reduction from Write-Once-Read-Many (WORM) memory semantics vs. traditional mutable memory on a 4-core RISC-V system simulated in gem5.

See `docs/plans/2026-03-09-worm-coherence-simulation-design.md` for the full design.

## Quick Start

```bash
# 1. Build gem5 (first time only, ~30-60 min)
bash scripts/build-gem5.sh

# 2. Cross-compile workloads
make -C workloads/mutable
make -C workloads/worm

# 3. Run the experiment
bash scripts/run-experiment.sh

# 4. View results
cat analysis/comparison.md
```

## Requirements

- Linux (Ubuntu 22.04+ recommended)
- `riscv64-linux-gnu-gcc` (apt: `gcc-riscv64-linux-gnu`)
- Python 3.8+
- SCons, build-essential, libprotobuf-dev, protobuf-compiler
- ~8 GB RAM for gem5 build
- ~30 GB disk for gem5 source + build
```

**Step 4: Add gem5 as a submodule pinned to v25.1.0.0**

```bash
cd /Users/zeblith/work/zeblithic/harmony-arch
git submodule add https://github.com/gem5/gem5.git gem5
cd gem5
git checkout v25.1.0.0
cd ..
```

Note: `git checkout v25.1.0.0` checks out the tag. If the exact tag doesn't exist, use the latest stable release tag. Check available tags with `git tag -l 'v2*' | sort -V | tail -5`.

**Step 5: Commit**

```bash
git add .gitignore .gitmodules gem5 README.md workloads/ configs/ scripts/ analysis/
git commit -m "scaffold: repository structure with gem5 submodule

Set up directory layout per design doc. gem5 pinned to v25.1.0.0.
Directories: workloads/{common,mutable,worm}, configs, scripts, analysis."
```

---

## Task 2: gem5 Build Script

**Files:**
- Create: `scripts/build-gem5.sh`

**Step 1: Write the build script**

```bash
#!/usr/bin/env bash
# Build gem5 for RISC-V with Ruby MESI_Two_Level coherence protocol.
#
# Usage: bash scripts/build-gem5.sh [--jobs N]
#
# Prerequisites (Ubuntu 22.04):
#   sudo apt install build-essential scons python3-dev libprotobuf-dev \
#     protobuf-compiler libgoogle-perftools-dev libboost-all-dev m4 zlib1g-dev

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GEM5_DIR="$REPO_ROOT/gem5"
BUILD_DIR="$GEM5_DIR/build/RISCV"
JOBS="${1:---jobs $(nproc)}"

if [[ "$1" == "--jobs" ]]; then
    JOBS="-j $2"
else
    JOBS="-j $(nproc)"
fi

echo "=== gem5 Build for RISC-V + MESI_Two_Level ==="
echo "gem5 dir:  $GEM5_DIR"
echo "build dir: $BUILD_DIR"
echo "parallel:  $JOBS"
echo ""

# Check gem5 submodule is initialized
if [ ! -f "$GEM5_DIR/SConstruct" ]; then
    echo "ERROR: gem5 submodule not initialized."
    echo "Run: git submodule update --init --recursive"
    exit 1
fi

cd "$GEM5_DIR"

# Check if already built
if [ -f "$BUILD_DIR/gem5.opt" ]; then
    echo "gem5.opt already exists at $BUILD_DIR/gem5.opt"
    echo "To rebuild, delete $BUILD_DIR and re-run."
    exit 0
fi

echo "Step 1/3: Creating build config for RISCV..."
scons defconfig build/RISCV build_opts/RISCV

echo ""
echo "Step 2/3: Setting protocol to MESI_Two_Level..."
scons setconfig build/RISCV PROTOCOL=MESI_Two_Level

echo ""
echo "Step 3/3: Building gem5.opt (this takes 30-60 minutes)..."
scons build/RISCV/gem5.opt $JOBS

echo ""
echo "=== Build complete ==="
echo "Binary: $BUILD_DIR/gem5.opt"
```

**Step 2: Make it executable and commit**

```bash
chmod +x scripts/build-gem5.sh
git add scripts/build-gem5.sh
git commit -m "feat: gem5 build script for RISC-V + MESI_Two_Level

Three-step build: defconfig → setconfig PROTOCOL → scons build.
Detects existing build, checks submodule init, auto-detects nproc."
```

---

## Task 3: Shared Workload Headers

**Files:**
- Create: `workloads/common/arena.h`
- Create: `workloads/common/config.h`

Both workloads share configuration constants and the WORM workload uses an arena allocator. Extracting these into shared headers keeps the two workloads in sync.

**Step 1: Write `workloads/common/config.h`**

```c
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
```

**Step 2: Write `workloads/common/arena.h`**

```c
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
```

**Step 3: Commit**

```bash
git add workloads/common/config.h workloads/common/arena.h
git commit -m "feat: shared workload headers (config + arena allocator)

config.h: shared constants (4 producers, 4KB blocks, 16 blocks each).
arena.h: append-only arena with atomic offset for WORM allocation."
```

---

## Task 4: Mutable Producer-Consumer Workload

**Files:**
- Create: `workloads/mutable/main.c`
- Create: `workloads/mutable/Makefile`

This is the "traditional" workload. Producers write into pre-allocated shared slots. Consumers spin-wait for ready slots, read data, compute checksum, mark slot free. This exercises the full MESI protocol: Modified→Invalid transitions, invalidation broadcasts, shared status array bouncing.

**Step 1: Write `workloads/mutable/main.c`**

```c
/* Mutable producer-consumer workload for WORM coherence simulation.
 *
 * This version uses traditional in-place mutation:
 * - Shared buffer pool with locks (spinlocks via atomics)
 * - Producers overwrite existing slots
 * - Status array bounces between cores
 *
 * This triggers the full MESI cache coherence protocol.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/config.h"

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

/* Deterministic data fill — same pattern as WORM version for checksum match.
 * Uses producer_id and block_index to generate reproducible data. */
static void fill_block(uint8_t *buf, int producer_id, int block_index) {
    uint32_t seed = (uint32_t)(producer_id * BLOCKS_PER_PRODUCER + block_index);
    for (int i = 0; i < BLOCK_SIZE; i++) {
        seed = seed * 1103515245 + 12345;  /* LCG */
        buf[i] = (uint8_t)(seed >> 16);
    }
}

/* Simple checksum — sum of all bytes as uint32. */
static uint32_t checksum(const uint8_t *buf, int len) {
    uint32_t sum = 0;
    for (int i = 0; i < len; i++) {
        sum += buf[i];
    }
    return sum;
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

        /* Wait for slot to be ready (should already be, but be safe) */
        while (atomic_load(&slot_status[slot]) != SLOT_READY) {
            /* Spin */
        }

        /* Read and checksum the data */
        local_checksum += checksum(buffer_pool[slot], BLOCK_SIZE);

        /* Mark slot free for reuse */
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
        pthread_create(&consumers[i], NULL, consumer_thread, (void *)(intptr_t)i);
    }

    /* Launch producers */
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_create(&producers[i], NULL, producer_thread, (void *)(intptr_t)i);
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
```

**Step 2: Write `workloads/mutable/Makefile`**

```makefile
# Makefile for mutable producer-consumer workload.
#
# Targets:
#   make          — cross-compile for RISC-V (gem5 target)
#   make native   — compile for host (sanity check)
#   make clean    — remove build artifacts

CC_RISCV = riscv64-linux-gnu-gcc
CC_NATIVE = gcc
CFLAGS = -O2 -Wall -Wextra -static -pthread
TARGET = mutable_prodcon

.PHONY: all native clean

all: $(TARGET)

$(TARGET): main.c ../common/config.h
	$(CC_RISCV) $(CFLAGS) -o $@ main.c

native: main.c ../common/config.h
	$(CC_NATIVE) -O2 -Wall -Wextra -pthread -o $(TARGET)_native main.c

clean:
	rm -f $(TARGET) $(TARGET)_native *.o
```

**Step 3: Sanity-check with native build**

```bash
cd workloads/mutable
make native
./mutable_prodcon_native
```

Expected output:
```
Mutable producer-consumer: 4 producers, 4 consumers, 64 blocks of 4096 bytes
Producer checksum: <some number>
Consumer checksum: <same number>
PASS: Checksums match.
```

**Step 4: Cross-compile for RISC-V**

```bash
make clean && make
file mutable_prodcon
```

Expected: `mutable_prodcon: ELF 64-bit LSB executable, UCB RISC-V, ...statically linked`

If `riscv64-linux-gnu-gcc` is not installed: `sudo apt install gcc-riscv64-linux-gnu`

**Step 5: Commit**

```bash
git add workloads/mutable/main.c workloads/mutable/Makefile
git commit -m "feat: mutable producer-consumer workload

Traditional in-place mutation pattern: shared buffer pool with spinlocks,
status array bouncing between cores. Exercises full MESI protocol.
Deterministic data fill with LCG for reproducible checksums."
```

---

## Task 5: WORM Producer-Consumer Workload

**Files:**
- Create: `workloads/worm/main.c`
- Create: `workloads/worm/Makefile`

This is the Harmony-style workload. Producers append to a shared arena (never overwrite). Consumers read from published offsets (immutable data). Single-writer mailboxes for coordination. No locks.

**Step 1: Write `workloads/worm/main.c`**

```c
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

/* Same deterministic fill as mutable version — checksums must match. */
static void fill_block(uint8_t *buf, int producer_id, int block_index) {
    uint32_t seed = (uint32_t)(producer_id * BLOCKS_PER_PRODUCER + block_index);
    for (int i = 0; i < BLOCK_SIZE; i++) {
        seed = seed * 1103515245 + 12345;  /* LCG */
        buf[i] = (uint8_t)(seed >> 16);
    }
}

/* Same checksum as mutable version. */
static uint32_t checksum(const uint8_t *buf, int len) {
    uint32_t sum = 0;
    for (int i = 0; i < len; i++) {
        sum += buf[i];
    }
    return sum;
}

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
        pthread_create(&consumers[i], NULL, consumer_thread, (void *)(intptr_t)i);
    }

    /* Launch producers */
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_create(&producers[i], NULL, producer_thread, (void *)(intptr_t)i);
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
```

**Step 2: Write `workloads/worm/Makefile`**

```makefile
# Makefile for WORM producer-consumer workload.
#
# Targets:
#   make          — cross-compile for RISC-V (gem5 target)
#   make native   — compile for host (sanity check)
#   make clean    — remove build artifacts

CC_RISCV = riscv64-linux-gnu-gcc
CC_NATIVE = gcc
CFLAGS = -O2 -Wall -Wextra -static -pthread
TARGET = worm_prodcon

.PHONY: all native clean

all: $(TARGET)

$(TARGET): main.c ../common/config.h ../common/arena.h
	$(CC_RISCV) $(CFLAGS) -o $@ main.c

native: main.c ../common/config.h ../common/arena.h
	$(CC_NATIVE) -O2 -Wall -Wextra -pthread -o $(TARGET)_native main.c

clean:
	rm -f $(TARGET) $(TARGET)_native *.o
```

**Step 3: Sanity-check with native build**

```bash
cd workloads/worm
make native
./worm_prodcon_native
```

Expected output:
```
WORM producer-consumer: 4 producers, 4 consumers, 64 blocks of 4096 bytes
Producer checksum: <same number as mutable version>
Consumer checksum: <same number as mutable version>
PASS: Checksums match.
```

**Step 4: Verify both workloads produce the same checksum**

```bash
cd /path/to/harmony-arch
MUTABLE=$(workloads/mutable/mutable_prodcon_native 2>&1 | grep "Producer checksum" | awk '{print $3}')
WORM=$(workloads/worm/worm_prodcon_native 2>&1 | grep "Producer checksum" | awk '{print $3}')
echo "Mutable: $MUTABLE"
echo "WORM:    $WORM"
[ "$MUTABLE" = "$WORM" ] && echo "MATCH" || echo "MISMATCH — BUG!"
```

**Step 5: Cross-compile for RISC-V**

```bash
make -C workloads/worm clean && make -C workloads/worm
file workloads/worm/worm_prodcon
```

Expected: `worm_prodcon: ELF 64-bit LSB executable, UCB RISC-V, ...statically linked`

**Step 6: Commit**

```bash
git add workloads/worm/main.c workloads/worm/Makefile
git commit -m "feat: WORM producer-consumer workload

Append-only arena, single-writer mailboxes, no locks, no overwrites.
Each cache line transitions I→Valid once and stays valid forever.
Uses same deterministic fill/checksum as mutable version for comparison."
```

---

## Task 6: gem5 Python Config Script

**Files:**
- Create: `configs/phase_a.py`

This script configures the gem5 simulation: 4-core RISC-V, Ruby MESI_Two_Level, Garnet2.0 mesh, SE mode. Uses the modern gem5 stdlib API.

**Step 1: Write `configs/phase_a.py`**

```python
"""gem5 configuration for Phase A: WORM coherence simulation.

4-core RISC-V TimingSimpleCPU with Ruby MESI_Two_Level coherence and
Garnet2.0 mesh interconnect. Syscall Emulation (SE) mode.

Usage:
    build/RISCV/gem5.opt configs/phase_a.py --cmd <binary> \
        [--num-cores 4] [--l1d-size 32kB] [--l2-size 1MB]
"""

import argparse

from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.cachehierarchies.ruby.mesi_two_level_cache_hierarchy import (
    MESITwoLevelCacheHierarchy,
)
from gem5.components.memory.single_channel import SingleChannelDDR3_1600
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.components.processors.cpu_types import CPUTypes
from gem5.isas import ISA
from gem5.resources.resource import BinaryResource
from gem5.simulate.simulator import Simulator


def parse_args():
    parser = argparse.ArgumentParser(
        description="Phase A: WORM coherence simulation config"
    )
    parser.add_argument(
        "--cmd", type=str, required=True,
        help="Path to the RISC-V binary to simulate"
    )
    parser.add_argument(
        "--num-cores", type=int, default=4,
        help="Number of CPU cores (default: 4)"
    )
    parser.add_argument(
        "--l1d-size", type=str, default="32kB",
        help="L1 data cache size per core (default: 32kB)"
    )
    parser.add_argument(
        "--l1d-assoc", type=int, default=8,
        help="L1 data cache associativity (default: 8)"
    )
    parser.add_argument(
        "--l1i-size", type=str, default="32kB",
        help="L1 instruction cache size per core (default: 32kB)"
    )
    parser.add_argument(
        "--l1i-assoc", type=int, default=8,
        help="L1 instruction cache associativity (default: 8)"
    )
    parser.add_argument(
        "--l2-size", type=str, default="1MB",
        help="Shared L2 cache size (default: 1MB)"
    )
    parser.add_argument(
        "--l2-assoc", type=int, default=16,
        help="L2 cache associativity (default: 16)"
    )
    return parser.parse_args()


def build_system(args):
    # Processor: N-core RISC-V TimingSimpleCPU
    processor = SimpleProcessor(
        cpu_type=CPUTypes.TIMING,
        isa=ISA.RISCV,
        num_cores=args.num_cores,
    )

    # Cache hierarchy: Ruby MESI Two-Level
    cache_hierarchy = MESITwoLevelCacheHierarchy(
        l1d_size=args.l1d_size,
        l1d_assoc=args.l1d_assoc,
        l1i_size=args.l1i_size,
        l1i_assoc=args.l1i_assoc,
        l2_size=args.l2_size,
        l2_assoc=args.l2_assoc,
        num_l2_banks=1,
    )

    # Memory: Single-channel DDR3
    memory = SingleChannelDDR3_1600(size="512MB")

    # Board: ties it all together in SE mode
    board = SimpleBoard(
        clk_freq="1GHz",
        processor=processor,
        memory=memory,
        cache_hierarchy=cache_hierarchy,
    )

    # Set the workload binary
    board.set_se_binary_workload(BinaryResource(local_path=args.cmd))

    return board


def main():
    args = parse_args()
    board = build_system(args)

    print(f"=== Phase A Configuration ===")
    print(f"Binary:     {args.cmd}")
    print(f"Cores:      {args.num_cores}x TimingSimpleCPU (RISC-V)")
    print(f"L1-D:       {args.l1d_size}, {args.l1d_assoc}-way")
    print(f"L1-I:       {args.l1i_size}, {args.l1i_assoc}-way")
    print(f"L2:         {args.l2_size}, {args.l2_assoc}-way (shared)")
    print(f"Protocol:   MESI_Two_Level")
    print(f"Memory:     512MB DDR3-1600")
    print(f"============================")

    simulator = Simulator(board=board)
    simulator.run()

    print(f"\nSimulation complete.")
    print(f"Stats written to: m5out/stats.txt")


if __name__ == "__m5_main__":
    main()
```

**Important:** gem5 uses `__m5_main__` as the module name, not `__main__`. This is a gem5 convention. If this doesn't work with the stdlib API, try `__main__` instead — the exact convention depends on the gem5 version. Check by looking at gem5's example configs:

```bash
grep -r '__m5_main__\|__main__' gem5/configs/example/ | head -5
```

**Step 2: Commit**

```bash
git add configs/phase_a.py
git commit -m "feat: gem5 config script for Phase A simulation

stdlib API: SimpleBoard + SimpleProcessor(TIMING, RISCV) +
MESITwoLevelCacheHierarchy + SingleChannelDDR3_1600.
Parameterized: core count, cache sizes, binary path."
```

---

## Task 7: Experiment Runner Script

**Files:**
- Create: `scripts/run-experiment.sh`

Automates the full comparison: compile both workloads, run gem5 on each, collect stats.

**Step 1: Write `scripts/run-experiment.sh`**

```bash
#!/usr/bin/env bash
# Run the Phase A WORM coherence experiment.
#
# 1. Cross-compile both workloads (if needed)
# 2. Run gem5 with the mutable workload
# 3. Run gem5 with the WORM workload
# 4. Run comparison script
#
# Usage: bash scripts/run-experiment.sh [--skip-build] [--num-cores 4]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GEM5="$REPO_ROOT/gem5/build/RISCV/gem5.opt"
CONFIG="$REPO_ROOT/configs/phase_a.py"
RESULTS_DIR="$REPO_ROOT/results"
NUM_CORES=4
SKIP_BUILD=false

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build) SKIP_BUILD=true; shift ;;
        --num-cores) NUM_CORES="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

echo "=== WORM Coherence Experiment (Phase A) ==="
echo "Cores: $NUM_CORES"
echo ""

# Check gem5 binary exists
if [ ! -f "$GEM5" ]; then
    echo "ERROR: gem5 not built. Run: bash scripts/build-gem5.sh"
    exit 1
fi

# Cross-compile workloads
if [ "$SKIP_BUILD" = false ]; then
    echo "--- Compiling workloads ---"
    make -C "$REPO_ROOT/workloads/mutable" clean all
    make -C "$REPO_ROOT/workloads/worm" clean all
    echo ""
fi

# Verify binaries exist
for bin in workloads/mutable/mutable_prodcon workloads/worm/worm_prodcon; do
    if [ ! -f "$REPO_ROOT/$bin" ]; then
        echo "ERROR: $bin not found. Compile first."
        exit 1
    fi
done

# Create results directories
mkdir -p "$RESULTS_DIR/mutable" "$RESULTS_DIR/worm"

# Run mutable workload
echo "--- Running mutable workload ---"
"$GEM5" \
    --outdir="$RESULTS_DIR/mutable" \
    "$CONFIG" \
    --cmd="$REPO_ROOT/workloads/mutable/mutable_prodcon" \
    --num-cores="$NUM_CORES"
echo ""

# Run WORM workload
echo "--- Running WORM workload ---"
"$GEM5" \
    --outdir="$RESULTS_DIR/worm" \
    "$CONFIG" \
    --cmd="$REPO_ROOT/workloads/worm/worm_prodcon" \
    --num-cores="$NUM_CORES"
echo ""

# Compare results
echo "--- Comparing results ---"
python3 "$REPO_ROOT/scripts/compare-stats.py" \
    "$RESULTS_DIR/mutable/stats.txt" \
    "$RESULTS_DIR/worm/stats.txt" \
    --output "$REPO_ROOT/analysis/comparison.md"

echo ""
echo "=== Experiment complete ==="
echo "Results:    $RESULTS_DIR/{mutable,worm}/stats.txt"
echo "Comparison: analysis/comparison.md"
```

**Step 2: Make executable and commit**

```bash
chmod +x scripts/run-experiment.sh
git add scripts/run-experiment.sh
git commit -m "feat: experiment runner script

Automates: compile workloads → gem5 mutable run → gem5 WORM run → compare.
Supports --skip-build and --num-cores flags."
```

---

## Task 8: Stats Comparison Script

**Files:**
- Create: `scripts/compare-stats.py`

Parses two gem5 `stats.txt` files and produces a side-by-side comparison table focused on coherence metrics.

**Step 1: Write `scripts/compare-stats.py`**

```python
#!/usr/bin/env python3
"""Compare gem5 stats from mutable vs. WORM simulation runs.

Extracts cache coherence metrics from Ruby stats and produces a
side-by-side comparison table in markdown format.

Usage:
    python3 scripts/compare-stats.py results/mutable/stats.txt results/worm/stats.txt \
        [--output analysis/comparison.md]
"""

import argparse
import re
import sys
from pathlib import Path


# Patterns to extract from stats.txt.
# Each entry: (display_name, regex_pattern, aggregate_function)
# aggregate_function: "sum" sums across all controllers, "first" takes the first match
METRICS = [
    # MESI state transitions (summed across all L1 controllers)
    ("I_to_M transitions", r"L1Cache_Controller.*\.I_to_M\s+(\d+)", "sum"),
    ("I_to_S transitions", r"L1Cache_Controller.*\.I_to_S\s+(\d+)", "sum"),
    ("M_to_I transitions", r"L1Cache_Controller.*\.M_to_I\s+(\d+)", "sum"),
    ("S_to_I transitions", r"L1Cache_Controller.*\.S_to_I\s+(\d+)", "sum"),
    ("M_to_S transitions", r"L1Cache_Controller.*\.M_to_S\s+(\d+)", "sum"),
    ("I_to_E transitions", r"L1Cache_Controller.*\.I_to_E\s+(\d+)", "sum"),
    ("E_to_I transitions", r"L1Cache_Controller.*\.E_to_I\s+(\d+)", "sum"),
    ("E_to_M transitions", r"L1Cache_Controller.*\.E_to_M\s+(\d+)", "sum"),

    # Network messages
    ("Request_Control msgs", r"msg_count\.Request_Control\s+(\d+)", "sum"),
    ("Response_Data msgs", r"msg_count\.Response_Data\s+(\d+)", "sum"),
    ("Writeback_Control msgs", r"msg_count\.Writeback_Control\s+(\d+)", "sum"),

    # Overall
    ("simTicks", r"^simTicks\s+(\d+)", "first"),
    ("simOps", r"^simOps\s+(\d+)", "first"),
]


def parse_stats(filepath: str) -> dict[str, int]:
    """Extract metrics from a gem5 stats.txt file."""
    text = Path(filepath).read_text()
    results = {}

    for name, pattern, agg in METRICS:
        matches = re.findall(pattern, text, re.MULTILINE)
        values = [int(m) for m in matches]

        if not values:
            results[name] = None
            continue

        if agg == "sum":
            results[name] = sum(values)
        elif agg == "first":
            results[name] = values[0]

    return results


def format_number(n: int | None) -> str:
    """Format a number with commas, or 'N/A' if missing."""
    if n is None:
        return "N/A"
    return f"{n:,}"


def compute_change(mutable_val: int | None, worm_val: int | None) -> str:
    """Compute percentage change from mutable to WORM."""
    if mutable_val is None or worm_val is None:
        return "N/A"
    if mutable_val == 0:
        if worm_val == 0:
            return "—"
        return "+∞"
    pct = ((worm_val - mutable_val) / mutable_val) * 100
    if pct < 0:
        return f"{pct:.1f}%"
    elif pct > 0:
        return f"+{pct:.1f}%"
    else:
        return "0.0%"


def generate_report(
    mutable_stats: dict[str, int],
    worm_stats: dict[str, int],
) -> str:
    """Generate a markdown comparison report."""
    lines = []
    lines.append("# WORM Coherence Simulation — Phase A Results")
    lines.append("")
    lines.append("| Metric | Mutable | WORM | Change |")
    lines.append("|--------|---------|------|--------|")

    for name, _, _ in METRICS:
        m = mutable_stats.get(name)
        w = worm_stats.get(name)
        change = compute_change(m, w)
        lines.append(
            f"| {name} | {format_number(m)} | {format_number(w)} | {change} |"
        )

    lines.append("")

    # Summary
    lines.append("## Key Observations")
    lines.append("")

    m_to_i_m = mutable_stats.get("M_to_I transitions")
    m_to_i_w = worm_stats.get("M_to_I transitions")
    if m_to_i_m is not None and m_to_i_w is not None:
        if m_to_i_w == 0:
            lines.append(
                "- **M→I transitions:** Zero in WORM (vs. "
                f"{format_number(m_to_i_m)} mutable) — "
                "confirms WORM eliminates forced writebacks."
            )
        elif m_to_i_m > 0:
            reduction = (1 - m_to_i_w / m_to_i_m) * 100
            lines.append(
                f"- **M→I transitions:** {reduction:.1f}% reduction in WORM."
            )

    req_m = mutable_stats.get("Request_Control msgs")
    req_w = worm_stats.get("Request_Control msgs")
    if req_m is not None and req_w is not None and req_m > 0:
        reduction = (1 - req_w / req_m) * 100
        lines.append(
            f"- **Request_Control messages:** {reduction:.1f}% reduction — "
            "fewer snoops and invalidation broadcasts."
        )

    ticks_m = mutable_stats.get("simTicks")
    ticks_w = worm_stats.get("simTicks")
    if ticks_m is not None and ticks_w is not None and ticks_m > 0:
        change_pct = ((ticks_w - ticks_m) / ticks_m) * 100
        if change_pct < 0:
            lines.append(
                f"- **Execution time:** {abs(change_pct):.1f}% faster "
                "(fewer stalls waiting for coherence traffic)."
            )
        else:
            lines.append(
                f"- **Execution time:** {change_pct:.1f}% slower "
                "(arena allocation overhead may exceed coherence savings at this scale)."
            )

    lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Compare gem5 coherence stats")
    parser.add_argument("mutable_stats", help="Path to mutable stats.txt")
    parser.add_argument("worm_stats", help="Path to WORM stats.txt")
    parser.add_argument(
        "--output", "-o", default=None,
        help="Output file (default: stdout)"
    )
    args = parser.parse_args()

    mutable = parse_stats(args.mutable_stats)
    worm = parse_stats(args.worm_stats)
    report = generate_report(mutable, worm)

    if args.output:
        Path(args.output).parent.mkdir(parents=True, exist_ok=True)
        Path(args.output).write_text(report)
        print(f"Report written to {args.output}")
    else:
        print(report)


if __name__ == "__main__":
    main()
```

**Step 2: Verify it handles missing files gracefully**

```bash
python3 scripts/compare-stats.py nonexistent1.txt nonexistent2.txt 2>&1 || true
```

Expected: FileNotFoundError (appropriate — no need to add error handling, the error message is clear).

**Step 3: Commit**

```bash
chmod +x scripts/compare-stats.py
git add scripts/compare-stats.py
git commit -m "feat: stats comparison script

Parses gem5 Ruby stats.txt, extracts MESI state transitions + network
messages, generates markdown table with percentage changes.
Auto-generates key observations for M→I, Request_Control, simTicks."
```

---

## Task 9: First Experiment Run

**Prerequisites:** This task requires the Linux machine with RISC-V cross-compiler and gem5 built. It cannot be done on macOS without Docker.

**Step 1: Build gem5 (if not done)**

```bash
bash scripts/build-gem5.sh
```

This takes 30-60 minutes. Monitor with `tail -f` on the scons output.

**Step 2: Install RISC-V cross-compiler (if not done)**

```bash
sudo apt install gcc-riscv64-linux-gnu
```

**Step 3: Cross-compile both workloads**

```bash
make -C workloads/mutable
make -C workloads/worm
```

Verify:
```bash
file workloads/mutable/mutable_prodcon
file workloads/worm/worm_prodcon
```

Both should say: `ELF 64-bit LSB executable, UCB RISC-V, ...statically linked`

**Step 4: Run native sanity check**

```bash
make -C workloads/mutable native && workloads/mutable/mutable_prodcon_native
make -C workloads/worm native && workloads/worm/worm_prodcon_native
```

Both must print `PASS: Checksums match.` with identical checksum values.

**Step 5: Run the experiment**

```bash
bash scripts/run-experiment.sh
```

This runs both gem5 simulations and produces `analysis/comparison.md`.

**Step 6: If gem5 config API doesn't work as written**

The stdlib API may differ between gem5 versions. If `phase_a.py` fails:

1. Check the error message — usually a missing import or renamed class.
2. Look at gem5's own examples: `ls gem5/configs/example/gem5_library/`
3. Key reference files:
   - `gem5/configs/example/gem5_library/riscv-ubuntu-run.py`
   - `gem5/src/python/gem5/components/` (browse available components)
4. Common fixes:
   - `BinaryResource(local_path=...)` might need just `Resource(...)` or the path passed differently
   - `__m5_main__` vs `__main__` for the entrypoint
   - `CPUTypes.TIMING` might be `CPUTypes.TimingSimpleCPU` in some versions

**Step 7: Inspect raw stats**

```bash
# Check that Ruby stats are present
grep -c "L1Cache_Controller" results/mutable/stats.txt
grep -c "L1Cache_Controller" results/worm/stats.txt

# Look at key metrics manually
grep "M_to_I\|I_to_M\|S_to_I\|msg_count" results/mutable/stats.txt
grep "M_to_I\|I_to_M\|S_to_I\|msg_count" results/worm/stats.txt
```

**Step 8: Review the comparison report**

```bash
cat analysis/comparison.md
```

Expected: M_to_I transitions near zero for WORM, significantly reduced Request_Control messages.

**Step 9: Commit the analysis**

```bash
git add analysis/comparison.md
git commit -m "results: Phase A baseline comparison

First experiment results: mutable vs. WORM producer-consumer on
4-core RISC-V with MESI_Two_Level coherence protocol."
```

---

## Task 10: Troubleshooting Guide

**Files:**
- Create: `docs/troubleshooting.md`

**Step 1: Write troubleshooting guide**

```markdown
# Troubleshooting

## gem5 Build Issues

### `scons: *** No SConstruct file found.`
You're not in the gem5 directory. The build script handles this — use `bash scripts/build-gem5.sh`.

### `ImportError: No module named 'gem5'`
Run the config script through gem5, not directly through python:
```
build/RISCV/gem5.opt configs/phase_a.py --cmd ...
```

### Build runs out of memory
Reduce parallelism: `bash scripts/build-gem5.sh --jobs 2`

## Cross-Compilation Issues

### `riscv64-linux-gnu-gcc: command not found`
```
sudo apt install gcc-riscv64-linux-gnu
```

### `Illegal instruction` in gem5
Compiled with wrong toolchain. Must use `riscv64-linux-gnu-gcc -static`, not `riscv64-unknown-elf-gcc`.

## gem5 Runtime Issues

### `fatal: syscall XXX unimplemented`
gem5 SE mode doesn't implement all Linux syscalls. Common fixes:
- Ensure static linking (`-static` flag)
- Avoid `printf` in hot loops (use only for final output)
- Avoid `malloc` in threads (pre-allocate in main)

### `Segfault in simulated program`
- Check the workload runs natively first (`make native && ./xxx_native`)
- Ensure `--num-cores` >= number of threads in the workload

### Stats file is empty or missing Ruby stats
- Verify gem5 was built with `PROTOCOL=MESI_Two_Level`
- Check `gem5/build/RISCV/config.log` for the protocol setting

## Checksum Mismatch
Both workloads must use the identical `fill_block()` function with the same LCG parameters. If checksums diverge, check:
- Same `BLOCKS_PER_PRODUCER` and `BLOCK_SIZE` in `config.h`
- Same seed calculation: `producer_id * BLOCKS_PER_PRODUCER + block_index`
- Same LCG constants: `1103515245` and `12345`
```

**Step 2: Commit**

```bash
git add docs/troubleshooting.md
git commit -m "docs: troubleshooting guide for gem5 build and runtime issues"
```

---

## Summary

| Task | What | Key Files |
|------|------|-----------|
| 1 | Repository scaffolding + gem5 submodule | `.gitignore`, `README.md`, `.gitmodules` |
| 2 | gem5 build script | `scripts/build-gem5.sh` |
| 3 | Shared workload headers | `workloads/common/{config,arena}.h` |
| 4 | Mutable workload | `workloads/mutable/{main.c,Makefile}` |
| 5 | WORM workload | `workloads/worm/{main.c,Makefile}` |
| 6 | gem5 config script | `configs/phase_a.py` |
| 7 | Experiment runner | `scripts/run-experiment.sh` |
| 8 | Stats comparison | `scripts/compare-stats.py` |
| 9 | First experiment run | `analysis/comparison.md` |
| 10 | Troubleshooting docs | `docs/troubleshooting.md` |

**Platform requirements:** Tasks 1-8 can be done on any machine (writing code). Task 9 requires Linux with `riscv64-linux-gnu-gcc` and a gem5 build (~30-60 min compile time, ~8 GB RAM).

**Total estimated gem5 simulation time:** ~5-15 minutes per workload on a modern machine (TimingSimpleCPU with small workload is fast).
