#!/usr/bin/env python3
"""
validate_docs.py
Validates documentation files in ArcadeMatrix to prevent doc drift.
"""
import os
import sys
import re

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DOCS_DIR = os.path.join(ROOT_DIR, "docs")

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

def main():
    print("🔍 Validating Documentation files...")
    all_ok = True
    for doc in REQUIRED_DOC_FILES:
        if not check_doc_file(doc):
            all_ok = False

    if all_ok:
        print("🎉 Documentation validation PASSED.")
        sys.exit(0)
    else:
        print("❌ Documentation validation FAILED.")
        sys.exit(1)

if __name__ == "__main__":
    main()
