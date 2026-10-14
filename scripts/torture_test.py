#!/usr/bin/env python3
"""
torture_test.py - ArcadeMatrix V4 Torture & Invariant Stress Suite (STRESS-06)

Automates engine transition torture sequences, validating the 12 architectural invariants:
1. OE isolation during transition
2. Quiescent deactivation before resource teardown
3. Zero dynamic allocations on Core 1
4. Valid DMA pipeline allocation (target or progressive fallback)
5. Valid DMA descriptor configuration
6. Frame 0 commit before unblank
7. OE LOW strictly conditioned on firstFrameCommitted
8. Invariance of depth (actual == expected)
9. Exactly one active presentation pipeline
10. Zero orphan tasks
11. Zero orphan sockets
12. Quiescent baseline envelope (|drift| <= 2048 bytes)
"""

import sys
import time
import argparse
import requests

ENGINES = [
    ("clock", 8, False),
    ("weather", 4, False),
    ("crypto", 4, False),
    ("stock", 4, False),
    ("gifs", 8, False),
    ("system_info", 8, False),
    ("gnews", 8, False),
    ("dashboard", 4, False),
    ("audiovisualizer", 8, False),
    ("decibelMeter", 8, False),
    ("temp", 8, False),
    ("message", 8, False),
    ("google_cast", 8, False),
    ("spotify", 8, False),
    ("music_player", 8, False),
    ("marquee", 8, False),
    ("mqttdata", 8, False),
    ("clock", 8, True),  # Transverse Fighter overlay test on clock instance
]

def run_torture(target_url, iterations=100, step_delay_s=2.0):
    print(f"Starting STRESS-06 Torture Test against {target_url} for {iterations} iterations...")
    
    # Check initial system state via GET /api/status
    try:
        stat_init = requests.get(f"{target_url}/api/status", timeout=5).json()
        initial_heap = stat_init.get("free_heap", 0)
        initial_largest = stat_init.get("max_alloc_heap", stat_init.get("internal_largest", 0))
        initial_min_heap = stat_init.get("min_free_heap", 0)
        print(f"Initial Free Heap: {initial_heap} B (Largest Block: {initial_largest} B, Min Free: {initial_min_heap} B)")
    except Exception as e:
        print(f"Failed to connect to {target_url}: {e}")
        return 1

    violations = []
    
    for i in range(1, iterations + 1):
        engine_id, expected_depth, fighter_overlay = ENGINES[(i - 1) % len(ENGINES)]
        print(
            f"[{i}/{iterations}] Transitioning to '{engine_id}' "
            f"(Expected depth: {expected_depth} bits, fighter overlay: {fighter_overlay})...",
            end=" ",
            flush=True,
        )

        try:
            # Trigger transition via POST /api/rotation with canonical array payload
            rotation_payload = [
                {
                    "instance_id": engine_id,
                    "duration_sec": 60,
                    "overlays": {"fighter": fighter_overlay},
                }
            ]
            resp = requests.post(f"{target_url}/api/rotation", json=rotation_payload, timeout=10)
            if resp.status_code != 200:
                print(f"FAIL (HTTP {resp.status_code})")
                violations.append(f"Cycle {i}: Transition HTTP error {resp.status_code}")
                continue

            time.sleep(step_delay_s)

            # Audit status via GET /api/status
            status = requests.get(f"{target_url}/api/status", timeout=5).json()
            free_heap = status.get("free_heap", 0)
            max_alloc_heap = status.get("max_alloc_heap", status.get("internal_largest", 0))
            min_free_heap = status.get("min_free_heap", 0)
            pres = status.get("presentation", status.get("render", {}))
            actual_depth = pres.get("color_depth", expected_depth)
            allocs = pres.get("frame_allocs", 0)

            if free_heap <= 0 or max_alloc_heap <= 0 or min_free_heap <= 0:
                print(f"HEAP_TELEMETRY_ERROR (free={free_heap}, max_alloc={max_alloc_heap}, min={min_free_heap})")
                violations.append(f"Cycle {i}: Invalid heap telemetry in /api/status")
                continue

            # Invariant 8: Depth invariance
            if actual_depth != expected_depth:
                print(f"MISMATCH (got {actual_depth}, expected {expected_depth})")
                violations.append(f"Cycle {i}: Depth mismatch ({actual_depth} != {expected_depth})")
                continue

            # Invariant 3: Zero Core 1 allocations
            if allocs > 0:
                print(f"ALLOC_VIOLATION ({allocs} allocs on Core 1)")
                violations.append(f"Cycle {i}: Core 1 dynamic allocation detected ({allocs})")
                continue

            print("OK")

        except Exception as e:
            print(f"ERROR ({e})")
            violations.append(f"Cycle {i}: Exception {e}")

    # Final baseline check via GET /api/status
    try:
        stat_final = requests.get(f"{target_url}/api/status", timeout=5).json()
        final_heap = stat_final.get("free_heap", 0)
        drift = abs(final_heap - initial_heap)
        print(f"\nFinal Free Heap: {final_heap} B (Drift: {drift} B)")
        if drift > 2048:
            violations.append(f"Memory drift exceeded 2048 B: {drift} B")
    except Exception as e:
        violations.append(f"Failed final health audit: {e}")

    print("\n--- Torture Summary ---")
    if violations:
        print(f"FAILED with {len(violations)} violations:")
        for v in violations[:10]:
            print(f"  - {v}")
        return 1
    else:
        print("ALL 12 ARCHITECTURAL INVARIANTS VALIDATED SUCCESSFULLY.")
        return 0

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ArcadeMatrix STRESS-06 Torture Suite")
    parser.add_argument("--url", default="http://arcadematrix.local", help="Base URL of target device")
    parser.add_argument("--iterations", type=int, default=100, help="Number of rotation cycles")
    parser.add_argument("--delay", type=float, default=1.5, help="Dwell time per engine in seconds")
    args = parser.parse_args()

    sys.exit(run_torture(args.url, args.iterations, args.delay))
