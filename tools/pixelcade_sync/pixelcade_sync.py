#!/usr/bin/env python3
"""
pixelcade_sync.py - one-shot PC-side tool to pre-cache Pixelcade marquee artwork onto a folder
you then copy to the ArcadeMatrix SD card, instead of the ESP32 (or a bridge script) fetching
images live from GitHub on every game launch.

Why this exists
----------------
ArcadeMatrix_RPi's core/dmd_cache.py downloads Pixelcade marquee images on demand from
https://raw.githubusercontent.com/alinke/pixelcade and caches them locally on the Pi's own disk.
The ESP32 has no equivalent: no spare flash/RAM budget for an HTTPS+TLS client doing on-demand
downloads mid-game, and no filesystem cache big enough to grow unbounded over time. Instead,
ArcadeMatrix's firmware (src/engines/RetroFrontendListener.cpp) expects the artwork to already be
sitting on the SD card at /pixelcade/<system>/<game>.png - this script is what populates that
folder, run once (or re-run occasionally to pick up new games) on a regular PC with a real
internet connection, not on the ESP32 itself.

Usage
-----
    python3 pixelcade_sync.py                      # sync every system Pixelcade has art for
    python3 pixelcade_sync.py --systems mame,snes,nes   # only sync the systems you actually use
    python3 pixelcade_sync.py --dest ./sdcard/pixelcade # custom output folder

Then copy the resulting folder to the root of your ArcadeMatrix SD card, so you end up with
paths like /pixelcade/mame/pacman.png on the card.

No third-party dependencies - stdlib only (urllib + zipfile), matching the project's convention
for PC-side tools (see tools/bdf_to_amfont/).
"""
import argparse
import io
import os
import shutil
import sys
import urllib.request
import zipfile

PIXELCADE_ZIP_URL = "https://github.com/alinke/pixelcade/archive/refs/heads/master.zip"
# The zip's top-level folder name (GitHub codeload convention: "<repo>-<branch>/").
ZIP_ROOT_PREFIX = "pixelcade-master/"

# Non-artwork paths inside the repo we never want to copy onto the SD card.
SKIP_DIR_NAMES = {".git", ".github", "scripts", "docs"}
VALID_EXTENSIONS = (".png", ".gif", ".jpg", ".jpeg")


def download_repo_zip(url: str) -> bytes:
    print(f"Downloading Pixelcade repository snapshot from {url} ...")
    req = urllib.request.Request(url, headers={"User-Agent": "ArcadeMatrix-pixelcade-sync"})
    with urllib.request.urlopen(req, timeout=60) as response:
        data = response.read()
    print(f"Downloaded {len(data) / (1024 * 1024):.1f} MB.")
    return data


def sync(dest_dir: str, systems_filter, zip_bytes: bytes) -> int:
    os.makedirs(dest_dir, exist_ok=True)
    copied = 0

    with zipfile.ZipFile(io.BytesIO(zip_bytes)) as zf:
        for info in zf.infolist():
            if info.is_dir():
                continue
            if not info.filename.startswith(ZIP_ROOT_PREFIX):
                continue

            rel_path = info.filename[len(ZIP_ROOT_PREFIX):]
            if not rel_path or "/" not in rel_path:
                continue  # top-level files (README, LICENSE, etc.) - not artwork

            system_folder = rel_path.split("/", 1)[0]
            if system_folder in SKIP_DIR_NAMES:
                continue
            if systems_filter and system_folder not in systems_filter:
                continue
            if not rel_path.lower().endswith(VALID_EXTENSIONS):
                continue

            target_path = os.path.join(dest_dir, rel_path)
            os.makedirs(os.path.dirname(target_path), exist_ok=True)
            with zf.open(info) as src, open(target_path, "wb") as dst:
                shutil.copyfileobj(src, dst)
            copied += 1

    return copied


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--dest", default="./pixelcade",
        help="Output folder to populate (copy its contents to /pixelcade on the SD card afterwards). Default: ./pixelcade"
    )
    parser.add_argument(
        "--systems", default=None,
        help="Comma-separated list of Pixelcade system folders to sync (e.g. mame,snes,nes). "
             "Default: sync every system Pixelcade has artwork for (large download, several hundred MB)."
    )
    parser.add_argument(
        "--zip-file", default=None,
        help="Use a local pixelcade-master.zip instead of downloading it (useful for repeated test runs)."
    )
    args = parser.parse_args()

    systems_filter = None
    if args.systems:
        systems_filter = {s.strip() for s in args.systems.split(",") if s.strip()}
        print(f"Filtering to systems: {', '.join(sorted(systems_filter))}")

    if args.zip_file:
        with open(args.zip_file, "rb") as f:
            zip_bytes = f.read()
    else:
        try:
            zip_bytes = download_repo_zip(PIXELCADE_ZIP_URL)
        except Exception as e:
            print(f"ERROR: failed to download Pixelcade repository: {e}", file=sys.stderr)
            return 1

    copied = sync(args.dest, systems_filter, zip_bytes)

    print(f"\nDone. Copied {copied} artwork files into {os.path.abspath(args.dest)}")
    print(f"Next step: copy the contents of '{args.dest}' onto your SD card's /pixelcade folder,")
    print("so paths look like /pixelcade/mame/pacman.png on the card.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
