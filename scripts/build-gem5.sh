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

# Parse args
JOBS="-j $(nproc)"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --jobs) JOBS="-j $2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

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
