#!/usr/bin/env python3
"""
generate_fonts.py - Batch-convert every .bdf font in your SD card's /fonts folder into
ArcadeMatrix's ESP32-loadable .amf format (see tools/bdf_to_amfont), in place.

Why: BitmapFontLoader (the ESP32 firmware's custom-font loader for the Clock, Date, and
scrolling Message) only understands the compact .amf binary format - it has no BDF parser
on-device (that would cost flash/RAM/CPU the firmware doesn't have to spare). This script
does the one-time offline conversion for you, the same way tools/gif_indexation's
generate_index.sh/.ps1 pre-processes GIFs for the ESP32 - just point it at your SD card.

What it does, for every *.bdf found directly in <sd_card_root_or_fonts_folder>/fonts:
  1. Converts it to a same-named .amf file (e.g. tom-thumb.bdf -> tom-thumb.amf).
  2. Deletes the source .bdf once the .amf is written successfully, so the SD card only
     carries the format the firmware can actually read (mirrors ArcadeMatrix_RPi, which
     loads .bdf directly - the ESP32 never needs the original file once converted).
  3. Leaves any file that fails to convert untouched and reports the error, rather than
     silently deleting a working source font.

Usage:
    python3 generate_fonts.py <path_to_sd_card_root_or_fonts_folder>

You can pass either the SD card root (a 'fonts' subfolder will be used automatically, same
convention as generate_index.sh) or the fonts folder itself.

No external dependencies: this script and tools/bdf_to_amfont/bdf_to_amfont.py both use only
the Python standard library. No venv/pip install/requirements.txt is needed to run it directly
with `python3`. See start_generate_fonts.sh/.bat for optional beginner-friendly wrappers that
set up a venv anyway, purely for consistency with the project's other tools.
"""
import os
import sys
import importlib.util

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BDF_TO_AMFONT_PATH = os.path.join(SCRIPT_DIR, "..", "bdf_to_amfont", "bdf_to_amfont.py")


def _load_converter():
    """Dynamically import convert() from tools/bdf_to_amfont/bdf_to_amfont.py without requiring
    it to be installed as a package or the caller's CWD to be any particular directory."""
    if not os.path.exists(BDF_TO_AMFONT_PATH):
        sys.exit(f"[ERROR] Could not find bdf_to_amfont.py at {BDF_TO_AMFONT_PATH}")
    spec = importlib.util.spec_from_file_location("bdf_to_amfont", BDF_TO_AMFONT_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.convert


def resolve_fonts_dir(path):
    """Accept either the SD root (auto-descend into ./fonts) or the fonts folder itself,
    matching generate_index.sh/.ps1's convention for /gifs."""
    if os.path.basename(os.path.normpath(path)) != "fonts":
        candidate = os.path.join(path, "fonts")
        if os.path.isdir(candidate):
            path = candidate
    return path


def main():
    if len(sys.argv) != 2:
        print("Usage: python3 generate_fonts.py <path_to_sd_card_root_or_fonts_folder>")
        print("  You can pass either the SD card root (a 'fonts' subfolder will be used")
        print("  automatically) or the fonts folder itself.")
        sys.exit(1)

    fonts_dir = resolve_fonts_dir(sys.argv[1])

    if not os.path.isdir(fonts_dir):
        sys.exit(f"[ERROR] '{fonts_dir}' does not exist. Pass your SD card root or its fonts/ folder.")

    convert = _load_converter()

    bdf_files = sorted(
        f for f in os.listdir(fonts_dir)
        if f.lower().endswith(".bdf") and os.path.isfile(os.path.join(fonts_dir, f))
    )

    if not bdf_files:
        print(f"No .bdf files found in {fonts_dir}. Nothing to do.")
        return

    converted = 0
    failed = 0
    for bdf_name in bdf_files:
        bdf_path = os.path.join(fonts_dir, bdf_name)
        amf_path = os.path.join(fonts_dir, os.path.splitext(bdf_name)[0] + ".amf")
        try:
            convert(bdf_path, amf_path, 0x20, 0x7E)
            os.remove(bdf_path)
            converted += 1
        except Exception as e:
            print(f"[ERROR] Failed to convert '{bdf_name}': {e}")
            failed += 1

    print(f"\nDone! Converted {converted} font(s) to .amf in {fonts_dir}"
          + (f" ({failed} failed, left untouched)." if failed else "."))


if __name__ == "__main__":
    main()
