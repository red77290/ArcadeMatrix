#!/usr/bin/env python3
import os
import sys
import re
import json

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SD_CONF_PATH = os.path.join(ROOT_DIR, "release", "sdCard", "config.json")

REQUIRED_DOC_FILES = [
    "README.md",
    "README_FR.md",
    "README_ES.md",
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
]

# Obsolete pattern checks
OBSOLETE_PATTERNS = [
    (re.compile(r'\bNTPSERVER\b'), "Obsolete config key 'NTPSERVER' found (use NTP_SERVER)"),
    (re.compile(r'\bFORMAT24H\b'), "Obsolete config key 'FORMAT24H' found (use FORMAT_24H)"),
    (re.compile(r'\bCOLOR_DEPTH\b'), "Obsolete config key 'COLOR_DEPTH' found (use PWM_BITS)"),
    (re.compile(r'\bSPRITE_COUNT\b'), "Obsolete config key 'SPRITE_COUNT' found"),
]

EXPECTED_CONFIG_SCHEMA = {
    "matrix": ["width", "height", "chain_length", "pwm_bits"],
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

def check_sd_config_json():
    if not os.path.exists(SD_CONF_PATH):
        print(f"❌ SD Card config.json file missing: {SD_CONF_PATH}")
        return False

    try:
        with open(SD_CONF_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:
        print(f"❌ release/sdCard/config.json parsing error: {e}")
        return False

    errors = 0
    for section, req_keys in EXPECTED_CONFIG_SCHEMA.items():
        if section not in data:
            print(f"❌ release/sdCard/config.json: Missing expected object '{section}'.")
            errors += 1
            continue
        for k in req_keys:
            if k not in data[section]:
                print(f"❌ release/sdCard/config.json: Missing required key '{k}' in object '{section}'.")
                errors += 1

    if "instances" not in data or not isinstance(data["instances"], list):
        print(f"❌ release/sdCard/config.json: Missing or invalid 'instances' array.")
        errors += 1

    if "rotation" not in data or not isinstance(data["rotation"], list):
        print(f"❌ release/sdCard/config.json: Missing or invalid 'rotation' array.")
        errors += 1

    if errors == 0:
        print("  ✓ release/sdCard/config.json structure valid.")
        return True
    return False

def main():
    print("🔍 Validating Documentation files & SD config.json...")
    all_ok = True
    for doc in REQUIRED_DOC_FILES:
        if not check_doc_file(doc):
            all_ok = False

    if not check_sd_config_json():
        all_ok = False

    if all_ok:
        print("🎉 Documentation & SD Config validation PASSED.")
        sys.exit(0)
    else:
        print("💥 Documentation & SD Config validation FAILED.")
        sys.exit(1)

if __name__ == "__main__":
    main()
