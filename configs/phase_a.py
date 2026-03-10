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


if __name__ in ("__m5_main__", "__main__"):
    main()
