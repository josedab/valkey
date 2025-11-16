#!/usr/bin/env python3
"""
Benchmark Comparison Tool for Valkey

This tool compares two benchmark result files and detects performance regressions.
It parses the JSON output from performance-benchmark.sh and analyzes the differences.
"""

import json
import sys
import re
import argparse
from typing import Dict, List, Tuple, Optional


def parse_benchmark_output(output: str) -> Dict[str, float]:
    """
    Parse valkey-benchmark output and extract requests per second for each operation.

    Example output format:
    "PING_INLINE: 74626.87 requests per second"
    "GET: 85470.09 requests per second, p50=0.279 msec"
    """
    results = {}

    for line in output.split('\n'):
        line = line.strip()
        if not line or line.startswith('#'):
            continue

        # Try to match "OPERATION: RPS requests per second"
        match = re.search(r'([A-Z_]+):\s+([\d.]+)\s+requests per second', line)
        if match:
            operation = match.group(1)
            rps = float(match.group(2))
            results[operation] = rps

    return results


def load_benchmark_file(filepath: str) -> Dict[str, Dict[str, float]]:
    """
    Load a benchmark results JSON file and parse all benchmarks.

    Returns a dict mapping benchmark name to operation results.
    """
    with open(filepath, 'r') as f:
        data = json.load(f)

    all_results = {}

    for benchmark in data.get('benchmarks', []):
        name = benchmark.get('name', 'unknown')
        output = benchmark.get('output', '')

        results = parse_benchmark_output(output)
        if results:
            all_results[name] = results

    return all_results


def compare_results(base_results: Dict[str, float],
                   pr_results: Dict[str, float],
                   threshold: float) -> Tuple[List[str], List[str], List[str]]:
    """
    Compare two sets of benchmark results.

    Returns: (regressions, improvements, unchanged)
    """
    regressions = []
    improvements = []
    unchanged = []

    for operation in base_results:
        if operation not in pr_results:
            continue

        base_rps = base_results[operation]
        pr_rps = pr_results[operation]

        # Calculate percentage change
        if base_rps > 0:
            change_pct = ((pr_rps - base_rps) / base_rps) * 100.0
        else:
            change_pct = 0.0

        result_str = f"{operation}: {base_rps:.2f} -> {pr_rps:.2f} ({change_pct:+.2f}%)"

        if change_pct < -threshold:
            regressions.append(result_str)
        elif change_pct > threshold:
            improvements.append(result_str)
        else:
            unchanged.append(result_str)

    return regressions, improvements, unchanged


def print_comparison_report(base_file: str, pr_file: str, threshold: float):
    """
    Print a detailed comparison report.
    """
    print("=" * 80)
    print("Valkey Performance Regression Analysis")
    print("=" * 80)
    print(f"Base:      {base_file}")
    print(f"PR:        {pr_file}")
    print(f"Threshold: {threshold}%")
    print("=" * 80)
    print()

    base_results = load_benchmark_file(base_file)
    pr_results = load_benchmark_file(pr_file)

    all_regressions = []
    all_improvements = []
    all_unchanged = []

    # Compare each benchmark category
    for benchmark_name in sorted(set(base_results.keys()) | set(pr_results.keys())):
        base_ops = base_results.get(benchmark_name, {})
        pr_ops = pr_results.get(benchmark_name, {})

        if not base_ops or not pr_ops:
            print(f"⚠️  Benchmark '{benchmark_name}' missing in one of the files")
            continue

        regressions, improvements, unchanged = compare_results(base_ops, pr_ops, threshold)

        all_regressions.extend(regressions)
        all_improvements.extend(improvements)
        all_unchanged.extend(unchanged)

        if regressions or improvements:
            print(f"\n📊 Benchmark: {benchmark_name}")
            print("-" * 80)

            if regressions:
                print(f"\n  ⚠️  REGRESSIONS (worse than {threshold}%):")
                for reg in regressions:
                    print(f"    - {reg}")

            if improvements:
                print(f"\n  ✅ IMPROVEMENTS (better than {threshold}%):")
                for imp in improvements:
                    print(f"    + {imp}")

    # Summary
    print("\n" + "=" * 80)
    print("SUMMARY")
    print("=" * 80)
    print(f"Total operations compared: {len(all_regressions) + len(all_improvements) + len(all_unchanged)}")
    print(f"Regressions:  {len(all_regressions)}")
    print(f"Improvements: {len(all_improvements)}")
    print(f"Unchanged:    {len(all_unchanged)}")
    print("=" * 80)

    return len(all_regressions)


def main():
    parser = argparse.ArgumentParser(
        description='Compare Valkey benchmark results and detect regressions'
    )
    parser.add_argument('base_file', help='Baseline benchmark results (JSON)')
    parser.add_argument('pr_file', help='PR benchmark results (JSON)')
    parser.add_argument('--threshold', type=float, default=5.0,
                       help='Regression threshold percentage (default: 5.0)')

    args = parser.parse_args()

    try:
        regression_count = print_comparison_report(
            args.base_file,
            args.pr_file,
            args.threshold
        )

        if regression_count > 0:
            print(f"\n❌ Found {regression_count} performance regression(s)")
            sys.exit(1)
        else:
            print("\n✅ No performance regressions detected")
            sys.exit(0)

    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(2)
    except json.JSONDecodeError as e:
        print(f"Error parsing JSON: {e}", file=sys.stderr)
        sys.exit(2)
    except Exception as e:
        print(f"Unexpected error: {e}", file=sys.stderr)
        sys.exit(2)


if __name__ == '__main__':
    main()
