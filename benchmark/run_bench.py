#!/usr/bin/env python3
"""
RaceLens Performance Benchmark Driver
Measures execution time with and without RaceLens instrumentation
and computes runtime overhead percentage.
"""

import subprocess
import time
import re
import sys

def run_bench(mode, runs=5):
    times = []
    for i in range(runs):
        start = time.perf_counter()
        res = subprocess.run(["./build/bench", mode], capture_output=True, text=True)
        end = time.perf_counter()
        
        # Extract elapsed from stdout
        match = re.search(r"Elapsed = ([0-9.]+) seconds", res.stdout)
        if match:
            times.append(float(match.group(1)))
        else:
            times.append(end - start)
    return sum(times) / len(times)

def main():
    print("=" * 65)
    print("           RaceLens Runtime Overhead Benchmark (2026)")
    print("=" * 65)
    print("Running warmup...")
    run_bench("baseline", runs=1)
    run_bench("monitored", runs=1)

    print("Measuring unmonitored baseline (5 iterations)...")
    avg_base = run_bench("baseline", runs=5)
    print(f"  -> Baseline average time:  {avg_base:.4f} s")

    print("Measuring RaceLens monitored execution (5 iterations)...")
    avg_mon = run_bench("monitored", runs=5)
    print(f"  -> Monitored average time: {avg_mon:.4f} s")

    overhead_pct = ((avg_mon - avg_base) / avg_base) * 100.0

    print("-" * 65)
    print(f"  Baseline Execution:  {avg_base*1000:.2f} ms")
    print(f"  Monitored Execution: {avg_mon*1000:.2f} ms")
    print(f"  Runtime Overhead:    +{overhead_pct:.1f}%")
    print("=" * 65)

if __name__ == "__main__":
    main()
