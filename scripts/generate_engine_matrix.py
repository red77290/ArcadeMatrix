#!/usr/bin/env python3
"""
generate_engine_matrix.py - Canonical Engine Compatibility Matrix Generator for ArcadeMatrix V4.

Architecture:
  C++ CompatibilityEvaluator (sole source of truth)
              │
  ┌───────────┴───────────┐
  ▼                       ▼
ESP32 runtime       Native C++ matrix_generator CLI
  │                       │
GET /api/engines          ▼
  │                 JSON artifact
  ▼                       │
WebUI                     ▼
                    Python formatter
                          │
                          ▼
            docs/ENGINE_COMPATIBILITY_MATRIX.md
            and CI validation (--check)
"""

import os
import sys
import shutil
import subprocess
import json
import time

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BUILD_DIR = os.path.join(PROJECT_ROOT, "build", "matrix_generator")
GENERATOR_BIN = os.path.join(BUILD_DIR, "matrix_generator")
DOC_PATH = os.path.join(PROJECT_ROOT, "docs", "ENGINE_COMPATIBILITY_MATRIX.md")

def find_compiler():
    for comp in ["g++", "clang++"]:
        path = shutil.which(comp)
        if path:
            return path
    return None

def ensure_arduinojson_include(include_dirs):
    for aj_path in [
        os.path.join(PROJECT_ROOT, ".pio", "libdeps", "esp32dev", "ArduinoJson", "src"),
        os.path.join(PROJECT_ROOT, ".pio", "libdeps", "esp32s3_waveshare", "ArduinoJson", "src"),
        os.path.join(PROJECT_ROOT, "build", "deps", "ArduinoJson", "src"),
    ]:
        if os.path.exists(os.path.join(aj_path, "ArduinoJson.h")):
            include_dirs.append(aj_path)
            return True

    # Try pio pkg install if available
    pio_bin = shutil.which("pio") or shutil.which("platformio")
    if pio_bin:
        try:
            print("📦 Installing PlatformIO library dependencies (ArduinoJson)...")
            subprocess.run(
                [pio_bin, "pkg", "install", "-e", "esp32dev", "--library", "bblanchon/ArduinoJson@^6.21.5", "--skip-dependencies", "--no-save"],
                cwd=PROJECT_ROOT, capture_output=True, text=True, check=False
            )
            for aj_path in [
                os.path.join(PROJECT_ROOT, ".pio", "libdeps", "esp32dev", "ArduinoJson", "src"),
                os.path.join(PROJECT_ROOT, ".pio", "libdeps", "esp32s3_waveshare", "ArduinoJson", "src"),
            ]:
                if os.path.exists(os.path.join(aj_path, "ArduinoJson.h")):
                    include_dirs.append(aj_path)
                    return True
        except Exception:
            pass

    # Fallback: Download standalone header
    try:
        import urllib.request
        fallback_dir = os.path.join(PROJECT_ROOT, "build", "deps", "ArduinoJson", "src")
        os.makedirs(fallback_dir, exist_ok=True)
        dest_file = os.path.join(fallback_dir, "ArduinoJson.h")
        if not os.path.exists(dest_file):
            print("🌐 Fetching standalone ArduinoJson.h for native host tools...")
            url = "https://github.com/bblanchon/ArduinoJson/releases/download/v6.21.5/ArduinoJson-v6.21.5.h"
            urllib.request.urlretrieve(url, dest_file)
        if os.path.exists(dest_file):
            include_dirs.append(fallback_dir)
            return True
    except Exception as e:
        print(f"⚠️ Failed to fetch ArduinoJson fallback: {e}")

    return False

def build_generator_binary():
    compiler = find_compiler()
    if not compiler:
        print("❌ ERROR: Neither g++ nor clang++ was found on PATH.")
        sys.exit(1)

    os.makedirs(BUILD_DIR, exist_ok=True)

    sources = [
        os.path.join(PROJECT_ROOT, "test", "native", "mock", "Serial.cpp"),
        os.path.join(PROJECT_ROOT, "src", "core", "CompatibilityEvaluator.cpp"),
        os.path.join(PROJECT_ROOT, "src", "core", "drawing", "PipelineSelectionPolicy.cpp"),
        os.path.join(PROJECT_ROOT, "test", "native", "tools", "matrix_generator.cpp"),
    ]

    for src in sources:
        if not os.path.exists(src):
            print(f"❌ ERROR: Source file not found: {src}")
            sys.exit(1)

    include_dirs = [
        os.path.join(PROJECT_ROOT, "test", "native", "mock"),
        os.path.join(PROJECT_ROOT, "include"),
        os.path.join(PROJECT_ROOT, "src"),
    ]

    ensure_arduinojson_include(include_dirs)

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
    cmd.extend(["-o", GENERATOR_BIN])

    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print("❌ Native matrix_generator compilation failed!")
        print(proc.stderr)
        sys.exit(1)

def run_matrix_generator():
    build_generator_binary()
    proc = subprocess.run([GENERATOR_BIN], capture_output=True, text=True)
    if proc.returncode != 0:
        print("❌ Execution of matrix_generator failed!")
        print(proc.stderr)
        sys.exit(1)
    try:
        return json.loads(proc.stdout)
    except Exception as e:
        print(f"❌ Failed to parse JSON from matrix_generator: {e}")
        print("Output was:")
        print(proc.stdout[:500])
        sys.exit(1)

def format_status(eval_obj):
    status = eval_obj.get("status", "unknown")
    fps = eval_obj.get("estimated_fps", 60)
    target = eval_obj.get("target_fps", 60)
    validated = eval_obj.get("empirically_validated", False)
    val_fps = eval_obj.get("validated_fps", 0)
    reason = eval_obj.get("primary_reason", "")
    strategy = eval_obj.get("strategy", "")

    if status == "compatible":
        if validated:
            return f"🟢 **{val_fps} FPS** (HW Validated)"
        return f"🟢 **{fps} FPS**"
    elif status == "degraded":
        return f"⚡ **{fps} FPS** ({reason})"
    else:
        return f"🚫 **Incompatible**<br>_{reason}_"

def generate_markdown(data):
    profiles = data.get("profiles", [])
    engines = data.get("engines", [])

    headers = ["Engine", "Category"]
    for p in profiles:
        headers.append(p.get("name", p["id"]))

    lines = [
        "# ArcadeMatrix Engine Compatibility & Performance Matrix",
        "",
        "> **Notice:** This document is automatically generated by `scripts/generate_engine_matrix.py`",
        "> using the canonical C++ `CompatibilityEvaluator` runtime engine as the sole source of truth.",
        "> Do not edit this file directly. Run `rtk python3 scripts/generate_engine_matrix.py` to regenerate.",
        "",
        "## 1. Summary Compatibility Matrix",
        "",
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join([":---"] * len(headers)) + " |",
    ]

    for eng in engines:
        name = eng.get("name", eng["id"])
        cat = eng.get("category", "")
        evals = eng.get("evaluations", {})
        
        row = [f"**{name}** (`{eng['id']}`)", f"`{cat}`"]
        for p in profiles:
            pid = p["id"]
            row.append(format_status(evals.get(pid, {})))

        lines.append("| " + " | ".join(row) + " |")

    lines.extend([
        "",
        "## 2. Hardware Resource & Peripheral Requirements",
        "",
        "| Engine ID | Target FPS | Double Buffer | DRAM Min / Contiguous | PSRAM Min | Required Peripherals |",
        "| :--- | :--- | :--- | :--- | :--- | :--- |",
    ])

    for eng in engines:
        eid = eng["id"]
        req = eng.get("requirements", {})
        tfps = eng.get("target_fps", 60)
        
        db = "Requires Double" if req.get("requires_double_buffer") else ("Prefers Double" if req.get("prefers_double_buffer") else "Single Buffer OK")
        dram_p = req.get("internal_persistent_bytes", 0) // 1024
        dram_c = req.get("internal_contiguous_bytes", 0) // 1024
        psram = req.get("psram_bytes", 0) // 1024
        
        periphs = []
        if req.get("needs_psram"): periphs.append("PSRAM")
        if req.get("needs_audio_input"): periphs.append("I2S Mic")
        if req.get("needs_temp_sensor"): periphs.append("SHTC3 Temp")
        if req.get("needs_gyroscope"): periphs.append("QMI8658 Gyro")
        if req.get("needs_network"): periphs.append("Wi-Fi")
        if req.get("needs_tls"): periphs.append("TLS/HTTPS")
        if req.get("needs_sd"): periphs.append("SD Card")
        periph_str = ", ".join(periphs) if periphs else "None"

        lines.append(f"| `{eid}` | {tfps} FPS | {db} | {dram_p} KB / {dram_c} KB | {psram} KB | {periph_str} |")

    lines.extend([
        "",
        "## 3. Admission Control Reserves",
        "",
        "To prevent system crashes and heap starvation during heavy network or audio activity, `CompatibilityEvaluator` enforces conservative admission reserves:",
        "",
        "- `ResourceReserve::TLS_SOCKET_ADMISSION_RESERVE`: **45,000 bytes** (mbedTLS context + TCP PCB)",
        "- `ResourceReserve::ASYNC_TCP_ADMISSION_RESERVE`: **16,000 bytes** (AsyncTCP socket pools)",
        "- `ResourceReserve::AUDIO_DMA_RING_ADMISSION_RESERVE`: **12,000 bytes** (I2S DMA ringbuffers)",
        "- `ResourceReserve::SYSTEM_MIN_HEADROOM_RESERVE`: **35,000 bytes** (FreeRTOS kernel, timer service, task stacks)",
        "",
        "Any engine requiring TLS or Wi-Fi will be safely marked **Incompatible** if the free internal heap cannot cover its persistent requirements *plus* these admission reserves and panel canvas requirements.",
        "",
        "---",
        f"*Generated by ArcadeMatrix V4 Architecture Tools on {time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime())}*",
        ""
    ])

    return "\n".join(lines)

def main():
    check_mode = "--check" in sys.argv
    json_mode = "--json" in sys.argv

    data = run_matrix_generator()

    if json_mode:
        print(json.dumps(data, indent=2))
        return

    md_content = generate_markdown(data)

    if check_mode:
        if not os.path.exists(DOC_PATH):
            print(f"❌ Document {DOC_PATH} does not exist!")
            sys.exit(1)
        with open(DOC_PATH, "r", encoding="utf-8") as f:
            existing = f.read()

        # Compare ignoring timestamp line
        def normalize(text):
            lines = [l for l in text.splitlines() if not l.startswith("*Generated by ArcadeMatrix")]
            return "\n".join(lines).strip()

        if normalize(existing) != normalize(md_content):
            print(f"❌ Documentation mismatch: {DOC_PATH} is out of sync with C++ CompatibilityEvaluator!")
            print("Run 'rtk python3 scripts/generate_engine_matrix.py' to update the documentation.")
            sys.exit(1)
        else:
            print(f"  ✓ {DOC_PATH} is up-to-date with C++ CompatibilityEvaluator.")
            sys.exit(0)
    else:
        with open(DOC_PATH, "w", encoding="utf-8") as f:
            f.write(md_content)
        print(f"✅ Generated {DOC_PATH} successfully from canonical C++ CompatibilityEvaluator.")

if __name__ == "__main__":
    main()
