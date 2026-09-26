#!/usr/bin/env python3
"""
scripts/package_sdcard.py
Automates validation and packaging of the ArcadeMatrix SD Card distribution bundle (ArcadeMatrix-sdcard.zip).
Ensures modular domain configuration (/config/) and legacy fallback (/config.json) are properly formatted.
"""

import os
import sys
import json
import shutil
import zipfile
import tempfile
import argparse

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SD_CARD_DIR = os.path.join(ROOT_DIR, "release", "sdCard")
DEFAULT_ZIP_PATH = os.path.join(ROOT_DIR, "ArcadeMatrix-sdcard.zip")

REQUIRED_MODULAR_FILES = [
    "hardware.json",
    "system.json",
    "network.json",
    "playlist.json",
]

EXCLUDE_PATTERNS = {
    ".DS_Store",
    "__pycache__",
    ".git",
    ".gitignore",
    ".bak",
}

def validate_sd_configuration():
    print("🔍 Validating SD card configuration...")
    errors = 0

    # 1. Validate modular domain configuration (/config/)
    conf_dir = os.path.join(SD_CARD_DIR, "config")
    if not os.path.isdir(conf_dir):
        print(f"❌ Missing modular config directory: {conf_dir}")
        return False

    for req_file in REQUIRED_MODULAR_FILES:
        fp = os.path.join(conf_dir, req_file)
        if not os.path.isfile(fp):
            print(f"❌ Missing modular config file: {fp}")
            errors += 1
            continue
        try:
            with open(fp, "r", encoding="utf-8") as f:
                data = json.load(f)
            if req_file == "hardware.json":
                if "render_pipeline" not in data and "renderPipeline" not in data:
                    print("❌ hardware.json is missing 'render_pipeline' key")
                    errors += 1
        except Exception as e:
            print(f"❌ Failed to parse {fp}: {e}")
            errors += 1

    # Check instances
    inst_dir = os.path.join(conf_dir, "instances")
    if not os.path.isdir(inst_dir):
        print(f"❌ Missing instances directory: {inst_dir}")
        errors += 1
    else:
        inst_files = [f for f in os.listdir(inst_dir) if f.endswith(".json")]
        if not inst_files:
            print(f"❌ No instance configurations found in {inst_dir}")
            errors += 1
        for ifile in inst_files:
            try:
                with open(os.path.join(inst_dir, ifile), "r", encoding="utf-8") as f:
                    json.load(f)
            except Exception as e:
                print(f"❌ Failed to parse {ifile}: {e}")
                errors += 1

    # 2. Validate legacy monolithic fallback (/config.json)
    legacy_file = os.path.join(SD_CARD_DIR, "config.json")
    if not os.path.isfile(legacy_file):
        print(f"❌ Missing legacy fallback: {legacy_file}")
        errors += 1
    else:
        try:
            with open(legacy_file, "r", encoding="utf-8") as f:
                leg_data = json.load(f)
            if "matrix" in leg_data and "render_pipeline" not in leg_data["matrix"]:
                print("❌ config.json matrix object is missing 'render_pipeline'")
                errors += 1
        except Exception as e:
            print(f"❌ Failed to parse legacy config.json: {e}")
            errors += 1

    if errors == 0:
        print("  ✓ All SD card configuration files are valid.")
        return True
    return False

def sync_tools_convenience_copy():
    """Ensure gif_indexation and tools are available in the bundle without unneeded cache files."""
    tools_src = os.path.join(ROOT_DIR, "tools")
    sd_tools_dst = os.path.join(SD_CARD_DIR, "tools")
    gif_idx_dst = os.path.join(SD_CARD_DIR, "gif_indexation")

    # If tools directory exists in main repo, synchronize gif_indexation convenience copy
    gif_idx_src = os.path.join(tools_src, "gif_indexation")
    if os.path.isdir(gif_idx_src):
        os.makedirs(gif_idx_dst, exist_ok=True)
        for item in os.listdir(gif_idx_src):
            if item.startswith(".") or item == "__pycache__":
                continue
            s = os.path.join(gif_idx_src, item)
            d = os.path.join(gif_idx_dst, item)
            if os.path.isfile(s):
                shutil.copy2(s, d)

def package_sdcard_zip(output_zip_path=DEFAULT_ZIP_PATH):
    sync_tools_convenience_copy()

    print(f"📦 Packaging SD card bundle into: {output_zip_path}...")
    file_count = 0
    total_bytes = 0

    with zipfile.ZipFile(output_zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for root, dirs, files in os.walk(SD_CARD_DIR):
            # Prune excluded directories
            dirs[:] = [d for d in dirs if not any(ex in d for ex in EXCLUDE_PATTERNS)]

            for file in files:
                if any(file.endswith(ex) or file == ex for ex in EXCLUDE_PATTERNS):
                    continue
                full_path = os.path.join(root, file)
                rel_path = os.path.relpath(full_path, SD_CARD_DIR)
                zf.write(full_path, arcname=rel_path)
                file_count += 1
                total_bytes += os.path.getsize(full_path)

    zip_size_mb = os.path.getsize(output_zip_path) / (1024 * 1024)
    print(f"  ✓ Packaged {file_count} files ({total_bytes / (1024 * 1024):.2f} MB uncompressed, {zip_size_mb:.2f} MB zip).")

    # Verify zip integrity
    print("🔍 Verifying package integrity...")
    with zipfile.ZipFile(output_zip_path, "r") as zf:
        namelist = set(zf.namelist())
        expected_entries = [
            "config/hardware.json",
            "config/system.json",
            "config/network.json",
            "config/playlist.json",
            "config.json",
            "README.md",
        ]
        for exp in expected_entries:
            if exp not in namelist:
                print(f"❌ Zip missing required file: {exp}")
                return False
        # Test CRC of all files in zip
        bad_file = zf.testzip()
        if bad_file:
            print(f"❌ Corrupted file in zip: {bad_file}")
            return False

    print("🎉 SD Card packaging & verification PASSED.")
    return True

def main():
    parser = argparse.ArgumentParser(description="Package and validate ArcadeMatrix SD card bundle.")
    parser.add_argument("--output", default=DEFAULT_ZIP_PATH, help="Output zip file path")
    parser.add_argument("--verify-only", action="store_true", help="Only validate configs without zipping")
    args = parser.parse_args()

    if not validate_sd_configuration():
        print("💥 SD card configuration validation failed.")
        sys.exit(1)

    if args.verify_only:
        print("🎉 Verification complete.")
        sys.exit(0)

    if not package_sdcard_zip(args.output):
        print("💥 SD card packaging failed.")
        sys.exit(1)

if __name__ == "__main__":
    main()
