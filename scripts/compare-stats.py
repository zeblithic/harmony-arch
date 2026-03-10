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
import sys
from pathlib import Path


# Patterns to extract from stats.txt.
# Each entry: (display_name, regex_pattern, aggregate_function)
# aggregate_function: "sum" sums across all controllers, "first" takes the first match
METRICS = [
    # MESI state transitions (summed across all L1 controllers).
    # Patterns match both gem5 naming conventions:
    #   stdlib API: "...L1Cache_Controller.L1Cache_Controller-0.I_to_M"
    #   raw Ruby:   "...l1_cntrl0.I_to_M"
    ("I_to_M transitions", r"(?:L1Cache_Controller|l1_cntrl)\w*\.I_to_M\s+(\d+)", "sum"),
    ("I_to_S transitions", r"(?:L1Cache_Controller|l1_cntrl)\w*\.I_to_S\s+(\d+)", "sum"),
    ("M_to_I transitions", r"(?:L1Cache_Controller|l1_cntrl)\w*\.M_to_I\s+(\d+)", "sum"),
    ("S_to_I transitions", r"(?:L1Cache_Controller|l1_cntrl)\w*\.S_to_I\s+(\d+)", "sum"),
    ("M_to_S transitions", r"(?:L1Cache_Controller|l1_cntrl)\w*\.M_to_S\s+(\d+)", "sum"),
    ("I_to_E transitions", r"(?:L1Cache_Controller|l1_cntrl)\w*\.I_to_E\s+(\d+)", "sum"),
    ("E_to_I transitions", r"(?:L1Cache_Controller|l1_cntrl)\w*\.E_to_I\s+(\d+)", "sum"),
    ("E_to_M transitions", r"(?:L1Cache_Controller|l1_cntrl)\w*\.E_to_M\s+(\d+)", "sum"),

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
    path = Path(filepath)
    if not path.exists():
        raise SystemExit(
            f"ERROR: stats file not found: {filepath}\n"
            "Did the gem5 simulation complete successfully?"
        )
    text = path.read_text()
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
            if reduction >= 0:
                lines.append(
                    f"- **M→I transitions:** {reduction:.1f}% reduction in WORM."
                )
            else:
                lines.append(
                    f"- **M→I transitions:** {abs(reduction):.1f}% increase in WORM "
                    "(unexpected — check for CAS retry overhead)."
                )

    req_m = mutable_stats.get("Request_Control msgs")
    req_w = worm_stats.get("Request_Control msgs")
    if req_m is not None and req_w is not None and req_m > 0:
        change = (1 - req_w / req_m) * 100
        if change >= 0:
            lines.append(
                f"- **Request_Control messages:** {change:.1f}% reduction — "
                "fewer snoops and invalidation broadcasts."
            )
        else:
            lines.append(
                f"- **Request_Control messages:** {abs(change):.1f}% increase — "
                "WORM arena/CAS overhead may be generating extra coherence traffic."
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

    # Warn about missing critical metrics (pattern mismatch with gem5 stat names)
    critical = {"M_to_I transitions", "I_to_M transitions", "Request_Control msgs"}
    for label, stats, path in [
        ("mutable", mutable, args.mutable_stats),
        ("WORM", worm, args.worm_stats),
    ]:
        missing = [k for k in critical if stats.get(k) is None]
        if missing:
            print(
                f"WARNING: {path}: metrics not found: {missing}\n"
                "Check that METRICS patterns match your gem5 build's stat names.",
                file=sys.stderr,
            )

    report = generate_report(mutable, worm)

    if args.output:
        Path(args.output).parent.mkdir(parents=True, exist_ok=True)
        Path(args.output).write_text(report)
        print(f"Report written to {args.output}")
    else:
        print(report)


if __name__ == "__main__":
    main()
