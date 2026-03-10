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
NUM_CORES=5
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
