#!/usr/bin/env python3
"""
Runs PlatformIO ESP32 unit tests inside QEMU emulator (qemu-system-xtensa)
and validates Unity test assertion results.
"""
import os
import sys
import subprocess
import time
import shutil
import re
import argparse

TEST_SUITES = [
    "test_retrofrontend",
    "test_utils",
    "test_engines",
    "test_core",
    "test_hardware",
    "test_api",
    "test_providers",
]

def find_qemu():
    qemu = shutil.which("qemu-system-xtensa")
    if qemu:
        return qemu
    for candidate in [
        os.path.expanduser("~/.qemu-xtensa/bin/qemu-system-xtensa"),
        "/usr/local/bin/qemu-system-xtensa",
        "/opt/qemu/bin/qemu-system-xtensa",
    ]:
        if os.path.exists(candidate):
            return candidate
    return None

def find_esptool():
    # 1. Prefer PlatformIO managed esptoolpy package if pio is installed
    if shutil.which("pio"):
        try:
            res = subprocess.run(["pio", "pkg", "exec", "-p", "tool-esptoolpy", "--", "esptool.py", "version"],
                                 capture_output=True, text=True)
            if res.returncode == 0:
                return ["pio", "pkg", "exec", "-p", "tool-esptoolpy", "--", "esptool.py"]
        except Exception:
            pass

    # 2. Check system PATH
    esptool_bin = shutil.which("esptool.py") or shutil.which("esptool")
    if esptool_bin:
        return [esptool_bin]

    # 3. Check PlatformIO tool package directory
    candidate = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")
    if os.path.exists(candidate):
        return [sys.executable, candidate]

    return [sys.executable, "-m", "esptool"]

def find_boot_app0():
    for candidate in [
        os.path.join("bin", "boot_app0.bin"),
        os.path.join("webinstaller", "bin", "boot_app0.bin"),
        os.path.expanduser("~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"),
    ]:
        if os.path.exists(candidate):
            return candidate
    return None

def build_test(suite, verbose=False):
    print(f"\n=======================================================")
    print(f"🔧 Compiling Test Suite: {suite}")
    print(f"=======================================================")
    cmd = ["pio", "test", "-e", "esp32dev", "-f", suite, "--without-uploading", "--without-testing"]
    try:
        res = subprocess.run(cmd, capture_output=not verbose, text=True, timeout=120)
        if res.returncode != 0:
            print(f"❌ Compilation failed for suite {suite}")
            if not verbose and res.stderr:
                print(res.stderr)
            return False
        return True
    except subprocess.TimeoutExpired:
        print(f"⏰ Compilation timeout (120s) reached for {suite}")
        return False

def merge_flash(suite, esptool_cmd, boot_app0, verbose=False):
    build_dir = os.path.join(".pio", "build", "esp32dev")
    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware = os.path.join(build_dir, "firmware.bin")
    merged_output = os.path.join(build_dir, f"{suite}_merged.bin")

    if not os.path.exists(bootloader) or not os.path.exists(partitions) or not os.path.exists(firmware):
        print(f"❌ Required build artifacts missing for {suite} in {build_dir}")
        return None

    if not boot_app0 or not os.path.exists(boot_app0):
        print("❌ boot_app0.bin could not be found!")
        return None

    merge_cmd = esptool_cmd + [
        "--chip", "esp32", "merge_bin",
        "-o", merged_output,
        "--flash_mode", "dio",
        "--flash_freq", "40m",
        "--flash_size", "4MB",
        "--fill-flash-size", "4MB",
        "0x1000", bootloader,
        "0x8000", partitions,
        "0xe000", boot_app0,
        "0x10000", firmware
    ]
    res = subprocess.run(merge_cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"❌ Failed to merge binary for {suite}: {res.stderr}")
        return None
    return merged_output

def run_qemu_suite(qemu_bin, flash_path, suite, verbose=False, timeout_sec=45):
    print(f"🚀 Executing {suite} in QEMU emulator...")
    cmd = [
        qemu_bin,
        "-nographic",
        "-monitor", "null",
        "-serial", "stdio",
        "-machine", "esp32",
        "-smp", "2",
        "-m", "4M",
        "-drive", f"file={flash_path},if=mtd,format=raw",
        "-no-reboot"
    ]

    stdout_data = ""
    try:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True
        )
        stdout_data, _ = proc.communicate(timeout=timeout_sec)
    except subprocess.TimeoutExpired:
        proc.kill()
        try:
            stdout_data, _ = proc.communicate(timeout=5)
        except Exception:
            pass
        print(f"⏰ Timeout ({timeout_sec}s) reached for {suite}")

    captured_lines = stdout_data.splitlines() if stdout_data else []
    passed = False
    failed = False
    tests_run = 0
    tests_failed = 0

    for line in captured_lines:
        stripped = line.strip()
        if verbose:
            print("    " + stripped)
        else:
            if ":" in stripped and ("PASSED" in stripped or "FAILED" in stripped):
                print("  " + stripped)
            elif "Tests " in stripped and "Failures " in stripped:
                print("  🏁 " + stripped)

        m = re.search(r"(\d+)\s+Tests\s+(\d+)\s+Failures", stripped)
        if m:
            tests_run = int(m.group(1))
            tests_failed = int(m.group(2))
            if tests_failed == 0 and tests_run > 0:
                passed = True
            else:
                failed = True

    if not passed and not verbose:
        print(f"\n--- [QEMU Output Dump for {suite}] ---")
        for l in captured_lines:
            print("  | " + l.rstrip())
        print("--------------------------------------\n")

    return passed and not failed

def main():
    parser = argparse.ArgumentParser(description="Run ArcadeMatrix unit tests in QEMU ESP32 emulator")
    parser.add_argument("-v", "--verbose", action="store_true", help="Enable verbose test output")
    parser.add_argument("-s", "--suite", type=str, help="Run a specific test suite")
    parser.add_argument("--no-build", action="store_true", help="Skip compilation and run existing build artifacts directly")
    args = parser.parse_args()

    qemu_bin = find_qemu()
    if not qemu_bin:
        print("❌ qemu-system-xtensa not found in PATH or ~/.qemu-xtensa/bin/")
        sys.exit(1)

    esptool_cmd = find_esptool()
    boot_app0 = find_boot_app0()

    print(f"✅ Found QEMU: {qemu_bin}")
    print(f"✅ Found esptool: {' '.join(esptool_cmd)}")
    print(f"✅ Found boot_app0: {boot_app0}")

    suites_to_run = [args.suite] if args.suite else TEST_SUITES
    results = {}

    for suite in suites_to_run:
        if not args.no_build:
            if not build_test(suite, verbose=args.verbose):
                results[suite] = "BUILD_FAILED"
                continue
        flash_path = merge_flash(suite, esptool_cmd, boot_app0, verbose=args.verbose)
        if not flash_path:
            results[suite] = "MERGE_FAILED"
            continue
        ok = run_qemu_suite(qemu_bin, flash_path, suite, verbose=args.verbose)
        results[suite] = "PASSED" if ok else "FAILED"

    print("\n" + "=" * 60)
    print("📊 QEMU UNIT TEST EXECUTION SUMMARY")
    print("=" * 60)
    all_ok = True
    for suite, res in results.items():
        icon = "✅" if res == "PASSED" else "❌"
        print(f"  {icon} {suite:<25} : {res}")
        if res != "PASSED":
            all_ok = False
    print("=" * 60)

    if all_ok:
        print("🎉 ALL TEST SUITES EXECUTED AND PASSED IN QEMU!")
        sys.exit(0)
    else:
        print("❌ SOME TEST SUITES FAILED IN QEMU!")
        sys.exit(1)

if __name__ == "__main__":
    main()
