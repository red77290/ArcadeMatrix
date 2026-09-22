#!/usr/bin/env python3
"""
Host Native C++ Test Runner for ArcadeMatrix.
Compiles and executes core business logic unit tests on the host platform
(macOS / Linux x86_64 in GitHub Actions CI) using g++ or clang++.

Guarantees 100% test execution in CI without hardware dependencies or QEMU,
with zero risk of contaminating release firmware binaries.
"""

import os
import sys
import shutil
import subprocess
import time

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BUILD_DIR = os.path.join(PROJECT_ROOT, "build", "native_tests")
RUNNER_BIN = os.path.join(BUILD_DIR, "runner")

def find_compiler():
    for comp in ["g++", "clang++"]:
        path = shutil.which(comp)
        if path:
            return path
    return None

def main():
    compiler = find_compiler()
    if not compiler:
        print("❌ ERROR: Neither g++ nor clang++ was found on PATH.")
        sys.exit(1)

    print(f"🔧 Using C++ compiler: {compiler}")
    os.makedirs(BUILD_DIR, exist_ok=True)

    sources = [
        os.path.join(PROJECT_ROOT, "test", "native", "unity", "unity.c"),
        os.path.join(PROJECT_ROOT, "test", "native", "mock", "Serial.cpp"),
        os.path.join(PROJECT_ROOT, "src", "core", "DisplayArbiter.cpp"),
        os.path.join(PROJECT_ROOT, "src", "core", "EngineRegistry.cpp"),
        os.path.join(PROJECT_ROOT, "test", "native", "test_native_core.cpp"),
    ]

    for src in sources:
        if not os.path.exists(src):
            print(f"❌ ERROR: Source file not found: {src}")
            sys.exit(1)

    include_dirs = [
        os.path.join(PROJECT_ROOT, "test", "native", "unity"),
        os.path.join(PROJECT_ROOT, "test", "native", "mock"),
        os.path.join(PROJECT_ROOT, "include"),
        os.path.join(PROJECT_ROOT, "src"),
    ]

    cmd = [
        compiler,
        "-std=c++17",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
    ]

    for inc in include_dirs:
        cmd.extend(["-I", inc])

    cmd.extend(sources)
    cmd.extend(["-o", RUNNER_BIN])

    print("🔨 Compiling native test runner...")
    t0 = time.time()
    compile_proc = subprocess.run(cmd, capture_output=True, text=True)
    if compile_proc.returncode != 0:
        print("❌ Compilation failed!")
        print(compile_proc.stderr)
        sys.exit(1)

    compile_time = time.time() - t0
    print(f"✅ Compilation succeeded in {compile_time:.2f}s -> {RUNNER_BIN}")

    print("🚀 Executing native unit test suite...\n" + "=" * 60)
    t0 = time.time()
    run_proc = subprocess.run([RUNNER_BIN], text=True)
    exec_time = time.time() - t0
    print("=" * 60)

    if run_proc.returncode == 0:
        print(f"🎉 All native tests passed successfully in {exec_time:.3f}s!")
        sys.exit(0)
    else:
        print(f"❌ Native tests failed with return code {run_proc.returncode}!")
        sys.exit(run_proc.returncode)

if __name__ == "__main__":
    main()
