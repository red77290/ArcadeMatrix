#!/usr/bin/env python3
"""
validate_webinstaller.py
Validates the Web Installer manifests and binary files for ArcadeMatrix.
"""
import os
import sys
import json

WEBINSTALLER_DIR = os.path.join(os.path.dirname(__file__), "..", "webinstaller")

EXPECTED_MANIFESTS = [
    {
        "filename": "manifest-esp32dev.json",
        "chipFamily": "ESP32",
        "expected_offsets": {4096, 32768, 57344, 65536}
    },
    {
        "filename": "manifest-esp32s3_waveshare.json",
        "chipFamily": "ESP32-S3",
        "expected_offsets": {0, 32768, 57344, 65536}
    }
]

def validate_manifest(manifest_info):
    filepath = os.path.join(WEBINSTALLER_DIR, manifest_info["filename"])
    if not os.path.exists(filepath):
        print(f"❌ Missing manifest file: {filepath}")
        return False

    try:
        with open(filepath, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:
        print(f"❌ Failed to parse JSON in {manifest_info['filename']}: {e}")
        return False

    builds = data.get("builds", [])
    if not builds:
        print(f"❌ No 'builds' array found in {manifest_info['filename']}")
        return False

    build = builds[0]
    chip_family = build.get("chipFamily")
    if chip_family != manifest_info["chipFamily"]:
        print(f"❌ Expected chipFamily '{manifest_info['chipFamily']}', got '{chip_family}' in {manifest_info['filename']}")
        return False

    parts = build.get("parts", [])
    if len(parts) < 4:
        print(f"❌ Expected at least 4 parts in {manifest_info['filename']}, found {len(parts)}")
        return False

    offsets_found = set()
    file_paths = []

    for part in parts:
        path = part.get("path")
        offset = part.get("offset")
        offsets_found.add(offset)
        file_paths.append(path)

        bin_full_path = os.path.join(WEBINSTALLER_DIR, path)
        # Note: In repo source, binary files might be generated during CI build
        if os.path.exists(bin_full_path):
            size = os.path.getsize(bin_full_path)
            if size == 0:
                print(f"❌ Binary file {path} exists but is empty (0 bytes)")
                return False
            print(f"  ✓ Found {path} ({size} bytes)")
        else:
            print(f"  ℹ Note: {path} not present locally (will be populated by CI build)")

    if offsets_found != manifest_info["expected_offsets"]:
        print(f"❌ Offsets mismatch in {manifest_info['filename']}. Expected {manifest_info['expected_offsets']}, found {offsets_found}")
        return False

    print(f"✅ Manifest {manifest_info['filename']} structure valid ({chip_family}).")
    return True

def main():
    print("🔍 Validating Web Installer Manifests...")
    all_ok = True
    for item in EXPECTED_MANIFESTS:
        if not validate_manifest(item):
            all_ok = False

    if all_ok:
        print("🎉 Web Installer validation PASSED.")
        sys.exit(0)
    else:
        print("❌ Web Installer validation FAILED.")
        sys.exit(1)

if __name__ == "__main__":
    main()
