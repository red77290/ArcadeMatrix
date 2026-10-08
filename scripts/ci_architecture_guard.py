#!/usr/bin/env python3
"""
CI Architecture Guard for ArcadeMatrix V4
-----------------------------------------
Enforces strict architectural boundaries:
1. Engines in src/engines/ MUST NOT reference:
   - MatrixEngine
   - MatrixPanel_I2S_DMA
   - FastMatrixPanel
   - Hub75BulkEncoder
   - Hub75DmaTarget
   - IPresentationBackend
   - IPresentationSynchronizer
   - Hardware register / DMA headers (<ESP32-HUB75-MatrixPanel-I2S-DMA.h>)
2. Engines must interact strictly through IDrawingSurface, EngineContract, and IEngine.
"""

import os
import sys
import re

FORBIDDEN_SYMBOLS_ENGINES = [
    r"\bMatrixEngine\b",
    r"\bMatrixPanel_I2S_DMA\b",
    r"\bFastMatrixPanel\b",
    r"\bHub75BulkEncoder\b",
    r"\bHub75DmaTarget\b",
    r"\bIPresentationBackend\b",
    r"\bIPresentationSynchronizer\b",
    r"<ESP32-HUB75-MatrixPanel-I2S-DMA\.h>",
    r'"ESP32-HUB75-MatrixPanel-I2S-DMA\.h"',
]

def scan_engines(root_dir):
    engines_dir = os.path.join(root_dir, "src", "engines")
    if not os.path.exists(engines_dir):
        print(f"[ERROR] Engines directory not found: {engines_dir}")
        return False

    violations = []
    engine_files = []
    for dirpath, _, filenames in os.walk(engines_dir):
        for f in filenames:
            if f.endswith((".cpp", ".h")):
                engine_files.append(os.path.join(dirpath, f))

    for filepath in engine_files:
        rel_path = os.path.relpath(filepath, root_dir)
        with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
            lines = f.readlines()

        for line_num, line in enumerate(lines, start=1):
            # Ignore comments
            clean_line = line.strip()
            if clean_line.startswith("//") or clean_line.startswith("*"):
                continue

            for pattern in FORBIDDEN_SYMBOLS_ENGINES:
                if re.search(pattern, clean_line):
                    violations.append((rel_path, line_num, clean_line, pattern))

    if violations:
        print("\n" + "=" * 80)
        print("❌ CI ARCHITECTURE GUARD VIOLATIONS DETECTED IN src/engines/:")
        print("=" * 80)
        for rel_path, line_num, line_content, pattern in violations:
            print(f"  {rel_path}:{line_num}: violates rule '{pattern}'")
            print(f"    Line: {line_content}")
        print("\nEngines MUST NOT access hardware, MatrixEngine, DMA, or BulkEncoder directly.")
        print("Engines MUST use IDrawingSurface abstraction only.\n")
        return False

    print(f"✅ CI Architecture Guard: 0 violations found across {len(engine_files)} engine files.")
    return True

if __name__ == "__main__":
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    success = scan_engines(repo_root)
    sys.exit(0 if success else 1)
