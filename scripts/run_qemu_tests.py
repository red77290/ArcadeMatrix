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
    candidate = os.path.expanduser("~/.qemu-xtensa/bin/qemu-system-xtensa")
    if os.path.exists(candidate):
        return candidate
    return None

def build_test(suite):
    print(f"\n=======================================================")
    print(f"🔧 Compiling Test Suite: {suite}")
    print(f"=======================================================")
    cmd = ["pio", "test", "-e", "esp32dev", "-f", suite, "--without-uploading", "--without-testing"]
    res = subprocess.run(cmd)
    if res.returncode != 0:
        print(f"❌ Compilation failed for suite {suite}")
        return False
    return True

def find_esptool():
    esptool_bin = shutil.which("esptool.py") or shutil.which("esptool")
    if esptool_bin:
        return [esptool_bin]
    candidate = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")
    if os.path.exists(candidate):
        return [sys.executable, candidate]
    return [sys.executable, "-m", "esptool"]

def merge_flash(suite):
    build_dir = os.path.join(".pio", "build", "esp32dev")
    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    boot_app0 = os.path.join("bin", "boot_app0.bin")
    firmware = os.path.join(build_dir, "firmware.bin")
    merged_output = os.path.join(build_dir, f"{suite}_merged.bin")

    if not os.path.exists(boot_app0):
        # Fallback if boot_app0.bin is in framework
        pkg_boot_app0 = os.path.expanduser("~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin")
        if os.path.exists(pkg_boot_app0):
            boot_app0 = pkg_boot_app0

    esptool_cmd = find_esptool()
    merge_cmd = esptool_cmd + [
        "--chip", "esp32", "merge_bin",
        "-o", merged_output,
        "--flash_mode", "dio",
        "--flash_freq", "40m",
        "--flash_size", "4MB",
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

def run_qemu_suite(qemu_bin, flash_path, suite, timeout_sec=40):
    print(f"🚀 Executing {suite} in QEMU emulator...")
    cmd = [
        qemu_bin,
        "-nographic",
        "-machine", "esp32",
        "-drive", f"file={flash_path},if=mtd,format=raw",
        "-serial", "mon:stdio",
        "-no-reboot"
    ]

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    start_time = time.time()
    passed = False
    failed = False
    tests_run = 0
    tests_failed = 0

    while True:
        if time.time() - start_time > timeout_sec:
            proc.kill()
            print(f"⏰ Timeout ({timeout_sec}s) reached for {suite}")
            return False

        line = proc.stdout.readline()
        if not line and proc.poll() is not None:
            break

        if line:
            stripped = line.strip()
            # Print unity test line
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
                    proc.kill()
                    break

    proc.poll()
    return passed and not failed

def main():
    qemu_bin = find_qemu()
    if not qemu_bin:
        print("❌ qemu-system-xtensa not found in PATH or ~/.qemu-xtensa/bin/")
        sys.exit(1)

    print(f"✅ Found QEMU: {qemu_bin}")

    results = {}
    for suite in TEST_SUITES:
        if not build_test(suite):
            results[suite] = "BUILD_FAILED"
            continue
        flash_path = merge_flash(suite)
        if not flash_path:
            results[suite] = "MERGE_FAILED"
            continue
        ok = run_qemu_suite(qemu_bin, flash_path, suite)
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
