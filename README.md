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
