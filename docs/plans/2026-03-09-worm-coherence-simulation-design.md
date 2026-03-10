# WORM Cache Coherence Simulation — Design Document

**Date:** 2026-03-09
**Status:** Approved
**Repo:** `zeblithic/harmony-arch`

---

## Problem

The Harmony protocol stack treats all data as Write-Once-Read-Many (WORM): 4KB pages are written once, cryptographically hashed, and never mutated. This eliminates entire classes of cache coherence traffic that dominate traditional multi-core systems — but nobody has *measured* the hardware-level impact. We need numbers.

## Hypothesis

A multi-threaded workload using WORM memory semantics will generate measurably less cache coherence traffic than an equivalent workload using traditional in-place mutation, on the same simulated multi-core RISC-V hardware. Specifically:

- Near-zero Modified→Invalid (M→I) cache state transitions
- Near-zero invalidation broadcasts across the interconnect
- Significant reduction in total coherence network bandwidth
- Modest reduction in total simulated execution cycles

## Approach

**Tool:** gem5 microarchitectural simulator in Syscall Emulation (SE) mode with the Ruby memory system.

**Two-phase experiment:**

- **Phase A:** Same hardware (stock MESI protocol), different software patterns. The WORM workload simply never overwrites shared data. Isolates the pure software benefit of WORM discipline on traditional hardware.
- **Phase B:** Custom hardware (Valid/Invalid-only coherence protocol via SLICC), WORM software. Quantifies the additional benefit of purpose-built WORM hardware — the overhead MESI's state machine itself imposes even when nothing is contended.

**ISA:** RISC-V (RV64GC). Chosen for clean gem5 support, extensibility (custom opcodes for future hash-verify instructions), and alignment with European sovereign silicon initiatives.

---

## Simulated Hardware Configuration

| Component | Specification | Notes |
|-----------|--------------|-------|
| ISA | RISC-V RV64GC | gem5 `RISCV` build target |
| Cores | 4x TimingSimpleCPU | Deterministic, cycle-approximate, fast simulation |
| L1-I per core | 32 KB, 8-way | Standard |
| L1-D per core | 32 KB, 8-way, 64B lines | Where coherence matters |
| L2 (shared) | 1 MB, 16-way, 64B lines | Unified across all 4 cores |
| Coherence protocol | MESI_Two_Level (Phase A) | Ruby's standard 2-level MESI |
| Interconnect | Garnet2.0 mesh | Models realistic inter-cache latency |
| Main memory | 512 MB DDR3 | More than sufficient for workloads |
| Execution mode | Syscall Emulation (SE) | No OS boot required |

This is a deliberately modest configuration optimized for fast iteration and clean signal, not production chip modeling.

---

## Workload Design: Producer-Consumer Benchmark

Two versions of the same logical program, compiled as separate RISC-V binaries. Both do identical useful work — the only difference is how they handle shared memory.

### Logical Task

4 producer threads each generate 4KB data blocks (simulating Harmony's page size). 4 consumer threads each read and process those blocks (compute a checksum). A coordinator tracks which blocks are ready.

### Version A: "Mutable" (Traditional)

```
Shared buffer pool: 64 pre-allocated 4KB slots

Producer[i]:
  1. Acquire lock on slot[next]
  2. Write 4KB of data INTO the slot (overwriting previous contents)
  3. Mark slot as "ready" (write to shared status array)
  4. Release lock

Consumer[i]:
  1. Spin on status array until a slot is "ready"
  2. Read 4KB from the slot
  3. Compute checksum
  4. Mark slot as "free" (write to shared status array)
```

This triggers the full MESI dance: producers put cache lines into Modified state, consumers must invalidate stale copies and fetch from the modifier, the status array bounces between cores constantly.

### Version B: "WORM" (Harmony-style)

```
Append-only arena: large contiguous allocation, monotonically growing offset

Producer[i]:
  1. Atomic fetch-add on global offset (claims next 4KB region)
  2. Write 4KB of data to the NEW region (never previously written)
  3. Publish the offset to a per-producer mailbox (single-writer)

Consumer[i]:
  1. Read from assigned producer's mailbox (single-writer/single-reader)
  2. Read 4KB from the published offset (immutable — will never change)
  3. Compute checksum
```

No locks. No overwrites. The arena only grows. Each cache line transitions from Invalid→Valid exactly once and stays Valid forever. Mailboxes are single-writer, so no coherence contention on coordination.

### Correctness Check

Both versions produce identical checksums. If they diverge, the workload has a bug.

---

## Experiment Protocol

### Execution

The `run-experiment.sh` script automates the full comparison:

1. Build gem5 (if not already built)
2. Cross-compile both workloads with `riscv64-unknown-elf-gcc` (static, newlib)
3. Run gem5 with mutable workload → `results/mutable/stats.txt`
4. Run gem5 with WORM workload → `results/worm/stats.txt`
5. Run `compare-stats.py` → `analysis/comparison.md`

### Parameter Sweeps

Once the baseline comparison works, vary parameters to test scaling:

| Parameter | Values | Question |
|-----------|--------|----------|
| Core count | 2, 4, 8 | Does WORM advantage grow with more cores? |
| Block count | 64, 256, 1024 | More sharing opportunities |
| Block size | 4KB, 64KB | Page-size alignment effects |

### Key Metrics from Ruby `stats.txt`

| Metric | What It Measures | Expected WORM Advantage |
|--------|-----------------|------------------------|
| `L1D.MESI.M_to_I` | Modified→Invalid transitions (forced writeback) | Near zero |
| `L1D.MESI.S_to_I` | Shared→Invalid transitions (invalidation) | Near zero |
| `L1D.MESI.I_to_M` | Invalid→Modified (write miss) | Same or lower |
| `network.msg_count.Request_Control` | Snoop/invalidation messages | Significantly lower |
| `network.msg_count.Response_Data` | Coherence data transfers | Lower |
| `simTicks` | Total simulated cycles | Possibly lower |

### Success Criteria (Phase A)

Statistically significant reductions in at least two of the above metrics when comparing WORM vs. mutable workloads on identical simulated hardware.

---

## Phase B: Custom SLICC Protocol (Future)

After Phase A proves the software-level WORM advantage, Phase B asks: "What if the hardware itself knew data was immutable?"

### WORM_Two_Level Protocol

Replace MESI's 4-state machine with a 2-state protocol:

| State | Meaning |
|-------|---------|
| `I` (Invalid) | Line not present in this cache |
| `V` (Valid) | Line present — immutable, will never change |

**Removed:** Modified state, Exclusive state, writeback logic, invalidation broadcasts, snoop responses.

**Remaining transitions:**
- `I → V`: Cache miss, fetch from L2/memory
- `V → I`: Eviction (capacity pressure only, never coherence-driven)

This is an ~80% reduction in protocol state machine complexity. gem5's SLICC language defines this as a new protocol alongside MESI, toggled by the Python config script.

### Phase B Comparison Table

The result becomes a 3-column comparison:

| Metric | Mutable/MESI | WORM/MESI | WORM/WORM_Protocol |
|--------|-------------|-----------|-------------------|
| (coherence metrics) | baseline | software gain | software + hardware gain |

The delta between columns 2 and 3 quantifies the overhead MESI's state machine itself imposes — the hardware-level argument for purpose-built WORM silicon.

---

## Repository Structure

```
harmony-arch/
├── docs/
│   └── plans/               # Design docs and implementation plans
├── gem5/                     # git submodule: gem5 source (pinned to stable)
├── workloads/
│   ├── common/               # Shared headers (arena allocator, timing helpers)
│   ├── mutable/              # Version A: traditional producer-consumer
│   │   ├── main.c
│   │   └── Makefile
│   └── worm/                 # Version B: WORM producer-consumer
│       ├── main.c
│       └── Makefile
├── configs/
│   ├── phase_a.py            # gem5 config: 4-core RISC-V, Ruby MESI_Two_Level
│   └── phase_b.py            # (Future) Custom SLICC protocol config
├── scripts/
│   ├── build-gem5.sh         # Build gem5 for RISC-V + Ruby
│   ├── run-experiment.sh     # Run both workloads, collect stats
│   └── compare-stats.py      # Parse stats.txt, generate comparison table
├── results/                  # gitignored — raw gem5 output
├── analysis/                 # Committed analysis reports
└── README.md
```

### Toolchain

- **gem5 build:** `scons build/RISCV/gem5.opt` with Ruby protocol `MESI_Two_Level`
- **Cross-compiler:** `riscv64-unknown-elf-gcc` (bare-metal newlib, static linking)
- **Analysis:** Python scripts with standard library (+ matplotlib for charts)

---

## Design Horizon: The Page-Local Compute Fabric

The following architectural intuitions inform the long-term direction of this research. They are not implemented in Phase A or B but shape future experiments (Phase C+).

### Core Insight: Constrain the Working Set

If a "runnable" (the smallest unit of scheduled computation) is limited to accessing at most 4 pages (4 × 4KB = 16KB) simultaneously, the memory controller becomes trivially simple. No TLB thrashing, no complex page table walks — just 4 fixed-size SRAM slots physically adjacent to each compute element.

### Checkerboard Compute Fabric

Inspired by Efficient Computer's Fabric architecture and the spatial dataflow paradigm:

```
┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐
│ MEM │ │ CPU │ │ MEM │ │ CPU │
│ 4KB │─│ 32b │─│ 4KB │─│ 32b │
└─────┘ └─────┘ └─────┘ └─────┘
    │       │       │       │
┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐
│ CPU │ │ MEM │ │ CPU │ │ MEM │
│ 32b │─│ 4KB │─│ 32b │─│ 4KB │
└─────┘ └─────┘ └─────┘ └─────┘
```

- **Memory tiles:** Each holds exactly one 4KB WORM page in SRAM
- **Compute tiles:** 32-bit processors, each can reach its 4 adjacent memory tiles
- **WORM enables sharing:** Because pages are immutable, a memory tile can serve reads to multiple adjacent compute tiles simultaneously without coherence concerns
- **Page rotation:** While a compute tile reads from 3 pages, the 4th memory tile can be reloaded with a new page — pipelined, zero-stall access

### The 32-bit Opportunity

The Harmony content model uses 256-bit global addresses but maps them into non-colliding 32-bit local namespaces via the "athenaeum" encoding (up to 3 blobs per 32-bit namespace). If the translation layer between 256-bit and 32-bit space is fast enough (dedicated hardware or a small number of 256-bit-capable "director" processors), the compute fabric can operate entirely in 32-bit space:

- **Director processors (few, large):** 256-bit-aware, handle book/blob resolution, athenaeum blueprint management, and orchestrate page placement across the fabric
- **Worker processors (many, tiny):** 32-bit, execute microfunctions on pages, communicate results via WORM writes to adjacent memory tiles

This hybrid mirrors the brain's architecture: a few high-level cortical regions (directors) orchestrating many parallel cerebellar elements (workers).

### Why This Matters for Simulation

Phase A/B prove that WORM eliminates coherence traffic. The page-local fabric takes the next logical step: if you don't need coherence, you don't need a shared bus, and if you don't need a shared bus, you can embed compute *inside* the memory topology. Future gem5 experiments (or custom simulators) can model:

- Energy cost of the 256→32 bit translation pipeline
- Throughput of the checkerboard fabric under various page rotation strategies
- Comparison of hybrid director/worker architectures vs. uniform 64-bit cores
- Whether the 4-page working set constraint is sufficient for real Harmony workloads (WASM runnables, Jain lifecycle operations, Zenoh subscription handlers)

These experiments build on Phase A/B's coherence traffic data to make the case for a fundamentally different chip topology.
