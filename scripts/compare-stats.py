#!/usr/bin/env python3
"""Compare gem5 stats from mutable vs. WORM simulation runs.

Extracts cache coherence metrics from Ruby stats and produces a
side-by-side comparison table in markdown format.

Usage:
    python3 scripts/compare-stats.py results/mutable/stats.txt results/worm/stats.txt \
        [--output analysis/comparison.md]
"""

from __future__ import annotations

import argparse
import re
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
    ("simTicks", r"(?:^|\.)simTicks\s+(\d+)", "first"),
    ("simOps", r"(?:^|\.)simOps\s+(\d+)", "first"),
]


def parse_stats(filepath: str) -> dict[str, int | None]:
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
    mutable_stats: dict[str, int | None],
    worm_stats: dict[str, int | None],
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
