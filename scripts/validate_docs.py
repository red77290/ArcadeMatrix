#!/usr/bin/env python3
"""
validate_docs.py
Validates documentation files and SD Card reference conf.ini in ArcadeMatrix to prevent doc drift.
"""
import os
import sys
import re

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DOCS_DIR = os.path.join(ROOT_DIR, "docs")
SD_CONF_PATH = os.path.join(ROOT_DIR, "release", "sdCard", "conf.ini")

REQUIRED_DOC_FILES = [
    "README.md", "README_FR.md", "README_ES.md",
    "docs/GETTING_STARTED.md", "docs/GETTING_STARTED_FR.md", "docs/GETTING_STARTED_ES.md",
    "docs/HARDWARE.md", "docs/HARDWARE_FR.md", "docs/HARDWARE_ES.md",
    "docs/WIRING.md", "docs/WIRING_FR.md", "docs/WIRING_ES.md",
    "docs/CONFIGURATION.md", "docs/CONFIGURATION_FR.md", "docs/CONFIGURATION_ES.md",
    "docs/DEVELOPER.md", "docs/DEVELOPER_FR.md", "docs/DEVELOPER_ES.md",
    "docs/ARCHITECTURE.md", "docs/ARCHITECTURE_FR.md", "docs/ARCHITECTURE_ES.md",
    "docs/ASSET_PIPELINE.md", "docs/ASSET_PIPELINE_FR.md", "docs/ASSET_PIPELINE_ES.md"
]

OBSOLETE_PATTERNS = [
    (re.compile(r'\bROTATION=.*sprites.*\b', re.IGNORECASE), "References obsolete 'sprites' in ROTATION string"),
    (re.compile(r'ArcadeMatrix-esp32s3\.zip\b'), "Refers to obsolete 'ArcadeMatrix-esp32s3.zip' instead of 'ArcadeMatrix-esp32s3_waveshare.zip'"),
]

def check_doc_file(rel_path):
    full_path = os.path.join(ROOT_DIR, rel_path)
    if not os.path.exists(full_path):
        print(f"❌ Missing required documentation file: {rel_path}")
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

def check_sd_conf_ini():
    if not os.path.exists(SD_CONF_PATH):
        print(f"❌ SD Card conf.ini file missing: {SD_CONF_PATH}")
        return False

    with open(SD_CONF_PATH, "r", encoding="utf-8") as f:
        content = f.read()

    errors = 0
    obsolete_sd_keys = [
        ("NTPSERVER=", "Obsolete key 'NTPSERVER=' found in SD conf.ini. Use 'NTP_SERVER=' instead."),
        ("FORMAT24H=", "Obsolete key 'FORMAT24H=' found in SD conf.ini. Use 'FORMAT_24H=' instead."),
        ("COLOR_DEPTH=", "Obsolete key 'COLOR_DEPTH=' found in SD conf.ini. Use 'PWM_BITS=' instead."),
        ("SPRITE_COUNT=", "Obsolete key 'SPRITE_COUNT=' found in SD conf.ini."),
    ]

    for key, msg in obsolete_sd_keys:
        if key in content:
            print(f"❌ release/sdCard/conf.ini: {msg}")
            errors += 1

    if re.search(r'\bROTATION=.*sprites.*\b', content, re.IGNORECASE):
        print("❌ release/sdCard/conf.ini: 'sprites' is listed in ROTATION sequence.")
        errors += 1

    if errors == 0:
        print("  ✓ release/sdCard/conf.ini structure and keys valid.")
        return True
    return False

def main():
    print("🔍 Validating Documentation files & SD conf.ini...")
    all_ok = True
    for doc in REQUIRED_DOC_FILES:
        if not check_doc_file(doc):
            all_ok = False

    if not check_sd_conf_ini():
        all_ok = False

    if all_ok:
        print("🎉 Documentation & SD Config validation PASSED.")
        sys.exit(0)
    else:
        print("❌ Documentation validation FAILED.")
        sys.exit(1)

if __name__ == "__main__":
    main()
