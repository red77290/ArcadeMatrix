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

ENGINES = [
    "clock",
    "weather",
    "crypto",
    "stock",
    "gifs",
    "system_info",
    "gnews",
    "dashboard",
    "audiovisualizer",
    "decibelMeter",
    "temp",
    "message",
    "google_cast",
    "spotify",
    "music_player",
    "marquee",
    "mqttdata",
]

def run_benchmark(target_url, duration_hours=24, sample_interval_s=30, output_csv="benchmark_results.csv", rotate_engines=False):
    print(f"Starting ArcadeMatrix Endurance Benchmark against {target_url} for {duration_hours}h...")
    start_time = time.time()
    end_time = start_time + (duration_hours * 3600)

    fields = [
        "timestamp", "uptime_s", "free_heap", "largest_block", "min_free_heap",
        "dma_free", "dma_largest", "fps", "presents", "frame_allocs", "frame_frees"
    ]

    initial_baseline = None
    max_alloc_detected = 0
    row = {}
    step_idx = 0

    with open(output_csv, "w", newline="") as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=fields)
        writer.writeheader()

        while time.time() < end_time:
            now = time.strftime("%Y-%m-%d %H:%M:%S")
            try:
                if rotate_engines:
                    eng = ENGINES[step_idx % len(ENGINES)]
                    use_fighter = (eng == "clock" and (step_idx // len(ENGINES)) % 2 == 1)
                    rotation_payload = [
                        {
                            "instance_id": eng,
                            "duration_sec": 60,
                            "overlays": {"fighter": use_fighter},
                        }
                    ]
                    requests.post(f"{target_url}/api/rotation", json=rotation_payload, timeout=5)
                    step_idx += 1

                stat_resp = requests.get(f"{target_url}/api/status", timeout=5).json()
                mem = stat_resp.get("memory", {})
                pres = stat_resp.get("presentation", stat_resp.get("render", {}))

                free_heap = stat_resp.get("free_heap", mem.get("free_heap", 0))
                largest_block = stat_resp.get(
                    "max_alloc_heap",
                    stat_resp.get("internal_largest", mem.get("largest_free_block", 0)),
                )
                min_free_heap = stat_resp.get("min_free_heap", mem.get("min_free_heap", 0))

                row = {
                    "timestamp": now,
                    "uptime_s": stat_resp.get("uptime", stat_resp.get("uptime_seconds", 0)),
                    "free_heap": free_heap,
                    "largest_block": largest_block,
                    "min_free_heap": min_free_heap,
                    "dma_free": stat_resp.get("dma_free", mem.get("free_dma_heap", 0)),
                    "dma_largest": stat_resp.get("dma_largest", mem.get("largest_dma_block", 0)),
                    "fps": pres.get("fps", stat_resp.get("fps", 0)),
                    "presents": pres.get("presents", 0),
                    "frame_allocs": pres.get("frame_allocs", 0),
                    "frame_frees": pres.get("frame_frees", 0),
                }

                if initial_baseline is None:
                    initial_baseline = row["free_heap"]

                alloc_count = row["frame_allocs"]
                if alloc_count > max_alloc_detected:
                    max_alloc_detected = alloc_count

                writer.writerow(row)
                csvfile.flush()

                print(f"[{now}] Heap: {row['free_heap']} B (Largest: {row['largest_block']} B, Min: {row['min_free_heap']} B) | "
                      f"FPS: {row['fps']} | Core 1 Allocs: {alloc_count}")

            except Exception as e:
                print(f"[{now}] WARNING: Telemetry sample failed: {e}")

            time.sleep(sample_interval_s)

    drift = abs(row.get("free_heap", 0) - initial_baseline) if initial_baseline else 0
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
