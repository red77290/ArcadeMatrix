#!/usr/bin/env python3
import os
import sys
import re
import json

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

REQUIRED_DOC_FILES = [
    "README.md",
    "README_FR.md",
    "README_ES.md",
    "docs/BEGINNER_GUIDE.md",
    "docs/BEGINNER_GUIDE_FR.md",
    "docs/BEGINNER_GUIDE_ES.md",
    "docs/GETTING_STARTED.md",
    "docs/GETTING_STARTED_FR.md",
    "docs/GETTING_STARTED_ES.md",
    "docs/HARDWARE.md",
    "docs/HARDWARE_FR.md",
    "docs/HARDWARE_ES.md",
    "docs/WIRING.md",
    "docs/WIRING_FR.md",
    "docs/WIRING_ES.md",
    "docs/CONFIGURATION.md",
    "docs/CONFIGURATION_FR.md",
    "docs/CONFIGURATION_ES.md",
    "docs/DEVELOPER.md",
    "docs/DEVELOPER_FR.md",
    "docs/DEVELOPER_ES.md",
    "docs/ARCHITECTURE.md",
    "docs/ARCHITECTURE_FR.md",
    "docs/ARCHITECTURE_ES.md",
    "docs/ASSET_PIPELINE.md",
    "docs/ASSET_PIPELINE_FR.md",
    "docs/ASSET_PIPELINE_ES.md",
    "docs/ENGINE_COMPATIBILITY_MATRIX.md",
    "docs/MEMORY_OPTIMIZATIONS.md",
    "docs/MEMORY_OPTIMIZATIONS_FR.md",
    "docs/MEMORY_OPTIMIZATIONS_ES.md",
]

# Obsolete pattern checks
OBSOLETE_PATTERNS = [
    (re.compile(r'\bNTPSERVER\b'), "Obsolete config key 'NTPSERVER' found (use NTP_SERVER)"),
    (re.compile(r'\bFORMAT24H\b'), "Obsolete config key 'FORMAT24H' found (use FORMAT_24H)"),
    (re.compile(r'\bSPRITE_COUNT\b'), "Obsolete config key 'SPRITE_COUNT' found"),
]

EXPECTED_CONFIG_SCHEMA = {
    "matrix": ["width", "height", "chain_length", "color_depth"],
    "system": ["timezone", "format_24h", "night_mode_enabled"],
    "wifi": ["ssid", "password"],
}

def check_doc_file(rel_path):
    full_path = os.path.join(ROOT_DIR, rel_path)
    if not os.path.exists(full_path):
        print(f"❌ Document file missing: {rel_path}")
        return False

    with open(full_path, "r", encoding="utf-8") as f:
        content = f.read()

    errors = 0
    for pattern, description in OBSOLETE_PATTERNS:
        if pattern.search(content):
            print(f"❌ {rel_path}: {description}")
            errors += 1

    if errors == 0:
        print(f"  ✓ {rel_path}")
        return True
    return False

def check_modular_sd_config():
    legacy_conf = os.path.join(ROOT_DIR, "release", "sdCard", "config.json")
    if os.path.exists(legacy_conf):
        print(f"❌ Obsolete monolithic config.json must not exist in release/sdCard/: {legacy_conf}")
        return False

    conf_dir = os.path.join(ROOT_DIR, "release", "sdCard", "config")
    if not os.path.isdir(conf_dir):
        print(f"❌ Modular SD Card config directory missing: {conf_dir}")
        return False

    required_files = ["hardware.json", "system.json", "network.json", "playlist.json"]
    for req in required_files:
        p = os.path.join(conf_dir, req)
        if not os.path.exists(p):
            print(f"❌ Modular SD config missing: {p}")
            return False
        try:
            with open(p, "r", encoding="utf-8") as f:
                d = json.load(f)
            if req == "hardware.json":
                if "render_pipeline" not in d and "renderPipeline" not in d:
                    print(f"❌ {p} missing required 'render_pipeline' key")
                    return False
                if "width" not in d or "height" not in d:
                    print(f"❌ {p} missing required 'width'/'height' keys")
                    return False
            elif req == "system.json":
                if "timezone" not in d or "night_mode_enabled" not in d:
                    print(f"❌ {p} missing required system keys")
                    return False
                if d.get("api_token", "") != "":
                    print(f"❌ {p} must ship with an empty 'api_token'")
                    return False
            elif req == "network.json":
                if "wifi" not in d or "ssid" not in d["wifi"] or "password" not in d["wifi"]:
                    print(f"❌ {p} missing required 'wifi.ssid' / 'wifi.password' keys")
                    return False
            elif req == "playlist.json":
                if not isinstance(d, list):
                    print(f"❌ {p} must be a JSON array")
                    return False
        except Exception as e:
            print(f"❌ {p} JSON error: {e}")
            return False

    inst_dir = os.path.join(conf_dir, "instances")
    if not os.path.isdir(inst_dir):
        print(f"❌ Modular SD config instances directory missing: {inst_dir}")
        return False

    print("  ✓ release/sdCard/config/ modular domain structure valid.")
    return True

def check_build_info():
    import subprocess
    build_info_path = os.path.join(ROOT_DIR, "src", "core", "BuildInfo.h")
    if not os.path.exists(build_info_path):
        print("❌ src/core/BuildInfo.h missing")
        return False
    with open(build_info_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "BUILD_GIT_COMMIT" not in content or "FIRMWARE_VERSION" not in content or "BUILD_TIMESTAMP" not in content:
        print("❌ src/core/BuildInfo.h missing required macros")
        return False

    # Extract commit hash macro
    m = re.search(r'#define\s+BUILD_GIT_COMMIT\s+"([^"]+)"', content)
    if not m:
        print("❌ src/core/BuildInfo.h has invalid BUILD_GIT_COMMIT format")
        return False
    header_commit = m.group(1).strip()

    # Check against git HEAD
    try:
        git_head = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=ROOT_DIR,
            stderr=subprocess.DEVNULL
        ).decode("utf-8").strip()

        if git_head and header_commit != git_head:
            print(f"  ℹ Synchronizing BuildInfo.h ({header_commit} -> {git_head})...")
            import runpy
            runpy.run_path(os.path.join(ROOT_DIR, "scripts", "build_webui.py"))
            with open(build_info_path, "r", encoding="utf-8") as f:
                updated = f.read()
            m2 = re.search(r'#define\s+BUILD_GIT_COMMIT\s+"([^"]+)"', updated)
            if not m2 or m2.group(1).strip() != git_head:
                print(f"❌ Failed to synchronize BuildInfo.h with git HEAD ({git_head})")
                return False
            header_commit = git_head
    except Exception:
        pass

    print(f"  ✓ src/core/BuildInfo.h valid and synchronized with HEAD ({header_commit}).")
    return True

def main():
    print("🔍 Validating Documentation files & SD modular config/...")
    all_ok = True
    for doc in REQUIRED_DOC_FILES:
        if not check_doc_file(doc):
            all_ok = False

    if not check_modular_sd_config():
        all_ok = False

    if not check_build_info():
        all_ok = False

    # Also run CI Architecture Guard
    import subprocess
    guard_script = os.path.join(ROOT_DIR, "scripts", "ci_architecture_guard.py")
    if os.path.exists(guard_script):
        ret = subprocess.call([sys.executable, guard_script], cwd=ROOT_DIR)
        if ret != 0:
            all_ok = False

    # Validate Engine Compatibility Matrix
    matrix_script = os.path.join(ROOT_DIR, "scripts", "generate_engine_matrix.py")
    if os.path.exists(matrix_script):
        ret = subprocess.call([sys.executable, matrix_script, "--check"], cwd=ROOT_DIR)
        if ret != 0:
            all_ok = False

    if all_ok:
        print("🎉 Documentation & SD Config validation PASSED.")
        sys.exit(0)
    else:
        print("💥 Documentation & SD Config validation FAILED.")
        sys.exit(1)

if __name__ == "__main__":
    main()
