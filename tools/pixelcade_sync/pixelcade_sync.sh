#!/usr/bin/env bash
# pixelcade_sync.sh - one-shot PC-side tool (macOS/Linux) to pre-cache Pixelcade marquee artwork
# into a folder you then copy onto the ArcadeMatrix SD card, instead of the ESP32 fetching images
# live from GitHub on every game launch (it has no spare flash/RAM/CPU budget for that - see
# docs/DEVELOPER.md's "Pixelcade-style marquee/box-art integration" section).
#
# Usage:
#   ./pixelcade_sync.sh                          # sync every system Pixelcade has art for
#   ./pixelcade_sync.sh mame,snes,nes             # only sync the systems you actually use
#   DEST=./sdcard/pixelcade ./pixelcade_sync.sh mame   # custom output folder
#
# Then copy the resulting folder onto the root of your SD card, so you end up with paths like
# /pixelcade/mame/pacman.png on the card.
#
# Requires only tools already on virtually every Mac/Linux box: curl (or wget) and unzip.
set -euo pipefail

PIXELCADE_ZIP_URL="https://github.com/alinke/pixelcade/archive/refs/heads/master.zip"
ZIP_ROOT_PREFIX="pixelcade-master"
DEST="${DEST:-./pixelcade}"
SYSTEMS_FILTER="${1:-}"

# --- Prerequisite checks (explicit, so a non-technical user knows exactly what to install) ---
missing=()
if ! command -v unzip >/dev/null 2>&1; then missing+=("unzip"); fi
if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then missing+=("curl or wget"); fi

if [ "${#missing[@]}" -gt 0 ]; then
    echo "ERROR: missing required tool(s): ${missing[*]}" >&2
    echo "" >&2
    echo "Install them with your OS package manager, e.g.:" >&2
    echo "  macOS:          brew install unzip curl" >&2
    echo "  Debian/Ubuntu:  sudo apt install unzip curl" >&2
    echo "  Fedora/RHEL:    sudo dnf install unzip curl" >&2
    exit 1
fi

mkdir -p "$DEST"
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

echo "Downloading Pixelcade repository snapshot from $PIXELCADE_ZIP_URL ..."
ZIP_FILE="$WORKDIR/pixelcade.zip"
if command -v curl >/dev/null 2>&1; then
    curl -fSL --progress-bar -A "ArcadeMatrix-pixelcade-sync" -o "$ZIP_FILE" "$PIXELCADE_ZIP_URL"
else
    wget -q --show-progress -U "ArcadeMatrix-pixelcade-sync" -O "$ZIP_FILE" "$PIXELCADE_ZIP_URL"
fi
echo "Downloaded $(du -h "$ZIP_FILE" | cut -f1)."

echo "Extracting ..."
unzip -q "$ZIP_FILE" -d "$WORKDIR/extracted"

SRC_ROOT="$WORKDIR/extracted/$ZIP_ROOT_PREFIX"
if [ ! -d "$SRC_ROOT" ]; then
    echo "ERROR: unexpected archive layout, '$ZIP_ROOT_PREFIX' folder not found after extraction." >&2
    exit 1
fi

# Non-artwork paths inside the repo we never want to copy onto the SD card.
SKIP_DIRS=(".git" ".github" "scripts" "docs")

copied=0
if [ -n "$SYSTEMS_FILTER" ]; then
    echo "Filtering to systems: $SYSTEMS_FILTER"
    IFS=',' read -ra SYSTEMS <<< "$SYSTEMS_FILTER"
else
    SYSTEMS=()
    for d in "$SRC_ROOT"/*/; do
        name="$(basename "$d")"
        skip=0
        for s in "${SKIP_DIRS[@]}"; do [ "$name" = "$s" ] && skip=1; done
        [ "$skip" -eq 0 ] && SYSTEMS+=("$name")
    done
fi

for system in "${SYSTEMS[@]}"; do
    system="$(echo "$system" | xargs)" # trim whitespace
    [ -z "$system" ] && continue
    src_dir="$SRC_ROOT/$system"
    if [ ! -d "$src_dir" ]; then
        echo "WARNING: no such system folder in Pixelcade repo: $system (skipped)" >&2
        continue
    fi
    mkdir -p "$DEST/$system"
    while IFS= read -r -d '' file; do
        cp "$file" "$DEST/$system/"
        copied=$((copied + 1))
    done < <(find "$src_dir" -maxdepth 1 -type f \( -iname "*.png" -o -iname "*.gif" -o -iname "*.jpg" -o -iname "*.jpeg" \) -print0)
done

echo ""
echo "Done. Copied $copied artwork files into $(cd "$DEST" && pwd)"
echo "Next step: copy the contents of '$DEST' onto your SD card's /pixelcade folder,"
echo "so paths look like /pixelcade/mame/pacman.png on the card."
