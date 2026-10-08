#!/usr/bin/env python3
"""
benchmark_endurance.py - ArcadeMatrix V4 24h Endurance Benchmark Harness

Interrogates an active ArcadeMatrix node over HTTP/Serial, sampling memory metrics,
rendering cadence, and Core 1 hot-path allocation counts.
"""

import sys
import time
import argparse
import requests
import csv

def run_benchmark(target_url, duration_hours=24, sample_interval_s=30, output_csv="benchmark_results.csv"):
    print(f"Starting ArcadeMatrix Endurance Benchmark against {target_url} for {duration_hours}h...")
    start_time = time.time()
    end_time = start_time + (duration_hours * 3600)

    fields = [
        "timestamp", "uptime_s", "free_heap", "largest_block", "min_free_heap",
        "dma_free", "dma_largest", "fps", "presents", "frame_allocs", "frame_frees"
    ]

    initial_baseline = None
    max_alloc_detected = 0

    with open(output_csv, "w", newline="") as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=fields)
        writer.writeheader()

        while time.time() < end_time:
            now = time.strftime("%Y-%m-%d %H:%M:%S")
            try:
                sys_resp = requests.get(f"{target_url}/api/system", timeout=5).json()
                stat_resp = requests.get(f"{target_url}/api/status", timeout=5).json()

                mem = sys_resp.get("memory", {})
                render = stat_resp.get("render", {})

                row = {
                    "timestamp": now,
                    "uptime_s": sys_resp.get("uptime_seconds", 0),
                    "free_heap": mem.get("free_heap", 0),
                    "largest_block": mem.get("largest_free_block", 0),
                    "min_free_heap": mem.get("min_free_heap", 0),
                    "dma_free": mem.get("free_dma_heap", 0),
                    "dma_largest": mem.get("largest_dma_block", 0),
                    "fps": render.get("fps", 0),
                    "presents": render.get("presents", 0),
                    "frame_allocs": render.get("frame_allocs", 0),
                    "frame_frees": render.get("frame_frees", 0),
                }

                if initial_baseline is None:
                    initial_baseline = row["free_heap"]

                alloc_count = row["frame_allocs"]
                if alloc_count > max_alloc_detected:
                    max_alloc_detected = alloc_count

                writer.writerow(row)
                csvfile.flush()

                print(f"[{now}] Heap: {row['free_heap']} B (Largest: {row['largest_block']} B) | "
                      f"FPS: {row['fps']} | Core 1 Allocs: {alloc_count}")

            except Exception as e:
                print(f"[{now}] WARNING: Telemetry sample failed: {e}")

            time.sleep(sample_interval_s)

    drift = abs(row["free_heap"] - initial_baseline) if initial_baseline else 0
    print("\n--- Benchmark Complete ---")
    print(f"Initial Baseline: {initial_baseline} B | Final Baseline: {row.get('free_heap', 0)} B | Drift: {drift} B")
    print(f"Max Core 1 Allocations: {max_alloc_detected}")

    passed = (max_alloc_detected == 0) and (drift <= 2048)
    print(f"Result: {'PASS' if passed else 'FAIL'}")
    return 0 if passed else 1

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ArcadeMatrix V4 Endurance Benchmark")
    parser.add_argument("--url", default="http://arcadematrix.local", help="Base URL of target device")
    parser.add_argument("--hours", type=float, default=24.0, help="Duration in hours")
    parser.add_argument("--interval", type=int, default=30, help="Sampling interval in seconds")
    parser.add_argument("--output", default="benchmark_endurance.csv", help="Output CSV path")
    args = parser.parse_args()

    sys.exit(run_benchmark(args.url, args.hours, args.interval, args.output))
