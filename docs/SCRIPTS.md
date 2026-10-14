# Build, Validation & Utility Scripts (ESP32)

The `scripts/` directory contains build hooks, architectural validators, test runners, and hardware endurance tools used both locally and in GitHub Actions CI.

---

## 1. Build & Pre-Commit Hooks

### 1.1 `scripts/build_webui.py`
Automates two vital tasks prior to firmware compilation:
1. **Cross-Contamination Guard & Minification**: Verifies that no Raspberry Pi hardware tokens exist in `data/index.html`, then bundles and compresses `data/index.html` into the C byte-array header `src/api/WebUI.h` served by the onboard async web server.
2. **Build Information Injection**: Extracts the current Git commit hash (`git rev-parse --short HEAD`) and UTC build timestamp, generating `src/core/BuildInfo.h` exposed via `/api/version`.

```bash
python3 scripts/build_webui.py
```
*(Automatically invoked via PlatformIO `extra_scripts` during build).*

### 1.2 `scripts/sync_build_info.py`
Synchronizes `src/core/BuildInfo.h` and `src/api/WebUI.h` during the Git `pre-commit` hook so build metadata and embedded WebUI headers are staged automatically within the active feature commit.

```bash
python3 scripts/sync_build_info.py
```

---

## 2. Test Runners & Matrix Generators

### 2.1 `scripts/run_native_tests.py`
Compiles and executes the hermetic off-target C++17 native unit test suite (`test/native/test_native_core.cpp`) on the host machine (macOS / Linux) in under 1 second. Validates the SRSW Triple-Buffer state machine, `DisplayArbiter` preemption, `CompatibilityEvaluator`, `RotationManager` lifecycle, and drawing surface invariants.

```bash
python3 scripts/run_native_tests.py
```

### 2.2 `scripts/run_qemu_tests.py`
Builds the PlatformIO embedded test binaries (`test_core`, `test_utils`, `test_engines`, `test_gifs`), merges the ESP32 flash image via `esptool.py`, and executes each suite inside `qemu-system-xtensa` (`espressif/qemu`) to verify FreeRTOS multi-tasking, memory alignment, and Unity assertions without physical hardware.

```bash
python3 scripts/run_qemu_tests.py
```

### 2.3 `scripts/generate_engine_matrix.py`
Compiles `test/native/tools/matrix_generator.cpp` against the canonical `CompatibilityEvaluator` and `ReferenceMemoryProfile` definitions to generate `docs/ENGINE_COMPATIBILITY_MATRIX.md`. In `--check` mode, verifies that the committed documentation matches the C++ evaluator output byte-for-byte.

```bash
# Regenerate docs/ENGINE_COMPATIBILITY_MATRIX.md:
python3 scripts/generate_engine_matrix.py

# Verify in CI:
python3 scripts/generate_engine_matrix.py --check
```

---

## 3. Static Validation & Quality Gates

### 3.1 `scripts/validate_docs.py`
Master documentation and repository quality gate. Runs `validate_ui_and_i18n.py`, checks `ENGINE_COMPATIBILITY_MATRIX.md` synchronization, verifies Markdown links across `docs/` and `README*.md`, checks OpenAPI spec (`docs/openapi.yaml`) coverage against `WebServerAPI.cpp` routes, and validates ADR formatting.

```bash
python3 scripts/validate_docs.py
```

### 3.2 `scripts/validate_ui_and_i18n.py`
Validates `data/index.html` and `src/core/I18n.h`:
- Enforces the hardware isolation invariant (rejecting any Raspberry Pi tokens in `data/index.html`).
- Verifies that all `data-i18n` keys and C++ `EngineDescriptor` `ConfigField` labels have complete translations in English (`en`), French (`fr`), and Spanish (`es`).

```bash
python3 scripts/validate_ui_and_i18n.py
```

### 3.3 `scripts/validate_webinstaller.py`
Validates the browser-based ESP Web Tools installer (`webinstaller/manifest-*.json` and `webinstaller/index.html`), checking JSON syntax, chip families (`ESP32`, `ESP32-S3`), partition offsets (`0x1000`/`0x0`, `0x8000`, `0xE000`, `0x10000`), and asset references.

```bash
python3 scripts/validate_webinstaller.py
```

---

## 4. Hardware Stress, Torture & Endurance Tools

### 4.1 `scripts/torture_test.py`
Live hardware chaos and endurance test runner over HTTP (`--host <ip> --duration <sec>`). Continuously cycles through canonical engines via `POST /api/rotation`, triggers rapid preemptive `POST /api/preview` overrides (`clock`, `message`), fires concurrent `GET /api/status` bursts, and asserts zero heap leaks and zero fragmentation collapse (`max_alloc_heap` / `internal_largest`).

```bash
python3 scripts/torture_test.py --host 192.168.1.50 --duration 60
```

### 4.2 `scripts/benchmark_endurance.py`
Long-duration hardware benchmark (`--host <ip> --duration 1800`) that rotates through all canonical engines while sampling `/api/status` every 2 seconds, writing a time-series CSV log of `free_heap`, `max_alloc_heap`, `min_free_heap`, and presentation telemetry, and enforcing strict memory stability thresholds.

```bash
python3 scripts/benchmark_endurance.py --host 192.168.1.50 --duration 1800 --csv endurance_report.csv
```

---

## 5. Arcade Console Integration

### 5.1 `scripts/sync-recalbox.sh`
Lightweight shell daemon deployed to Recalbox / Batocera / RetroPie consoles (automatically installable via `POST /api/mqtt/install` in the WebUI). Watches EmulationStation state events and publishes active system/game marquee updates to ArcadeMatrix over MQTT or HTTP.

