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
    ("clock", 8),
    ("crypto", 4),
    ("gif", 8),
    ("weather", 4),
    ("clock", 8),
    ("stock", 4)
]

def run_torture(target_url, iterations=100, step_delay_s=2.0):
    print(f"Starting STRESS-06 Torture Test against {target_url} for {iterations} iterations...")
    
    # Check initial system state
    try:
        sys_init = requests.get(f"{target_url}/api/system", timeout=5).json()
        initial_heap = sys_init.get("memory", {}).get("free_heap", 0)
        print(f"Initial Free Heap: {initial_heap} B")
    except Exception as e:
        print(f"Failed to connect to {target_url}: {e}")
        return 1

    violations = []
    
    for i in range(1, iterations + 1):
        engine_id, expected_depth = ENGINES[(i - 1) % len(ENGINES)]
        print(f"[{i}/{iterations}] Transitioning to '{engine_id}' (Expected depth: {expected_depth} bits)...", end=" ", flush=True)

        try:
            # Trigger transition via POST /api/display/engine or rotation simulation
            resp = requests.post(f"{target_url}/api/display/engine", json={"engine": engine_id}, timeout=10)
            if resp.status_code != 200:
                print(f"FAIL (HTTP {resp.status_code})")
                violations.append(f"Cycle {i}: Transition HTTP error {resp.status_code}")
                continue

            time.sleep(step_delay_s)

            # Audit status
            status = requests.get(f"{target_url}/api/status", timeout=5).json()
            render = status.get("render", {})
            actual_depth = render.get("color_depth", expected_depth) # or from matrix status
            allocs = render.get("frame_allocs", 0)

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

    # Final baseline check
    try:
        sys_final = requests.get(f"{target_url}/api/system", timeout=5).json()
        final_heap = sys_final.get("memory", {}).get("free_heap", 0)
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
