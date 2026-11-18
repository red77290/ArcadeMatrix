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

PIXELCADE_ZIP_URL="https://github.com/red77290/pixelcade/archive/refs/heads/master.zip"
ZIP_ROOT_PREFIX="pixelcade-master"
DEST="${DEST:-./pixelcade}"
SYSTEMS_FILTER="${1:-}"
LOCAL_ZIP="${LOCAL_ZIP:-}"

trim_str() {
    local v="${1:-}"
    v="${v#"${v%%[![:space:]]*}"}"
    v="${v%"${v##*[![:space:]]}"}"
    printf '%s' "$v"
}

# --- Prerequisite checks (Bash 3.2 + set -u safe on macOS) ---
missing_tools=""
if ! command -v unzip >/dev/null 2>&1; then
    missing_tools="unzip"
fi
if [ -z "$LOCAL_ZIP" ] && ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
    if [ -n "$missing_tools" ]; then
        missing_tools="$missing_tools, curl or wget"
    else
        missing_tools="curl or wget"
    fi
fi

if [ -n "$missing_tools" ]; then
    echo "ERROR: missing required tool(s): $missing_tools" >&2
    echo "" >&2
    echo "Install them with your OS package manager, e.g.:" >&2
    echo "  macOS:          brew install unzip curl" >&2
    echo "  Debian/Ubuntu:  sudo apt install unzip curl" >&2
    echo "  Fedora/RHEL:    sudo dnf install unzip curl" >&2
    exit 1
fi

mkdir -p "$DEST"
WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/pixelcade_sync.XXXXXX")"
trap 'rm -rf "$WORKDIR"' EXIT

if [ -n "$LOCAL_ZIP" ]; then
    if [ ! -f "$LOCAL_ZIP" ]; then
        echo "ERROR: LOCAL_ZIP file does not exist: $LOCAL_ZIP" >&2
        exit 1
    fi
    ZIP_FILE="$LOCAL_ZIP"
    echo "Using local Pixelcade archive: $ZIP_FILE"
else
    echo "Downloading Pixelcade repository snapshot from $PIXELCADE_ZIP_URL ..."
    ZIP_FILE="$WORKDIR/pixelcade.zip"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL --progress-bar -A "ArcadeMatrix-pixelcade-sync" -o "$ZIP_FILE" "$PIXELCADE_ZIP_URL"
    else
        wget -q --show-progress -U "ArcadeMatrix-pixelcade-sync" -O "$ZIP_FILE" "$PIXELCADE_ZIP_URL"
    fi
    echo "Downloaded $(du -h "$ZIP_FILE" | cut -f1)."
fi

echo "Extracting ..."
mkdir -p "$WORKDIR/extracted"
unzip -q "$ZIP_FILE" -d "$WORKDIR/extracted"

SRC_ROOT="$WORKDIR/extracted/$ZIP_ROOT_PREFIX"
if [ ! -d "$SRC_ROOT" ]; then
    # Fallback if branch archive prefix changed (e.g. pixelcade-main)
    for cand in "$WORKDIR/extracted"/*/; do
        if [ -d "$cand" ]; then
            SRC_ROOT="${cand%/}"
            break
        fi
    done
fi
if [ ! -d "$SRC_ROOT" ]; then
    echo "ERROR: unexpected archive layout, '$ZIP_ROOT_PREFIX' folder not found after extraction." >&2
    exit 1
fi

is_skipped_dir() {
    case "$1" in
        .git|.github|scripts|docs|_catalog|.*) return 0 ;;
        *) return 1 ;;
    esac
}

# Build canonical system folder list from extracted archive (Bash 3.2 + set -u safe)
AVAIL_SYSTEMS=()
avail_count=0
for d in "$SRC_ROOT"/*/; do
    [ -d "$d" ] || continue
    name="$(basename "$d")"
    if ! is_skipped_dir "$name"; then
        AVAIL_SYSTEMS+=("$name")
        avail_count=$((avail_count + 1))
    fi
done

if [ "$avail_count" -eq 0 ]; then
    echo "ERROR: no artwork system folders found in archive." >&2
    exit 1
fi

resolve_canonical_system() {
    local req_lc
    req_lc="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
    local cand cand_lc
    for cand in "${AVAIL_SYSTEMS[@]}"; do
        cand_lc="$(printf '%s' "$cand" | tr '[:upper:]' '[:lower:]')"
        if [ "$cand_lc" = "$req_lc" ]; then
            printf '%s' "$cand"
            return 0
        fi
    done
    return 1
}

SELECTED_SYSTEMS=()
add_selected_system() {
    local target="$1"
    local existing
    for existing in ${SELECTED_SYSTEMS[@]+"${SELECTED_SYSTEMS[@]}"}; do
        if [ "$existing" = "$target" ]; then
            return 0
        fi
    done
    SELECTED_SYSTEMS+=("$target")
}

if [ -n "$SYSTEMS_FILTER" ]; then
    echo "Filtering to systems: $SYSTEMS_FILTER (plus console logos)"
    IFS=',' read -ra RAW_SYSTEMS <<< "$SYSTEMS_FILTER"
    for raw in ${RAW_SYSTEMS[@]+"${RAW_SYSTEMS[@]}"}; do
        req="$(trim_str "$raw")"
        [ -z "$req" ] && continue
        case "$req" in
            .|..|*/*|*\\*)
                echo "WARNING: invalid system folder name: $req (skipped)" >&2
                continue
                ;;
        esac
        if canonical="$(resolve_canonical_system "$req")"; then
            add_selected_system "$canonical"
        else
            echo "WARNING: no such system folder in Pixelcade repo: $req (skipped)" >&2
        fi
    done
    if canonical_console="$(resolve_canonical_system "console")"; then
        add_selected_system "$canonical_console"
    fi
else
    for sys_name in "${AVAIL_SYSTEMS[@]}"; do
        add_selected_system "$sys_name"
    done
fi

copied=0
for system in ${SELECTED_SYSTEMS[@]+"${SELECTED_SYSTEMS[@]}"}; do
    src_dir="$SRC_ROOT/$system"
    [ -d "$src_dir" ] || continue
    mkdir -p "$DEST/$system"
    sys_count="$(find "$src_dir" -maxdepth 1 -type f \( -iname "*.png" -o -iname "*.gif" -o -iname "*.jpg" -o -iname "*.jpeg" \) | wc -l | tr -d '[:space:]')"
    if [ "${sys_count:-0}" -gt 0 ]; then
        find "$src_dir" -maxdepth 1 -type f \( -iname "*.png" -o -iname "*.gif" -o -iname "*.jpg" -o -iname "*.jpeg" \) \
            -exec sh -c 'dst="$1"; shift; cp -f -- "$@" "$dst/"' sh "$DEST/$system" {} +
        copied=$((copied + sys_count))
    fi
done

# Copy `_catalog/*.txt` manifests so offline ESP32 (no PSRAM / zero live TLS) resolves EU/USA/No-Intro filenames directly on SD
catalog_copied=0
if [ -d "$SRC_ROOT/_catalog" ]; then
    mkdir -p "$DEST/_catalog"
    if [ -f "$SRC_ROOT/_catalog/_folders.txt" ]; then
        cp -f -- "$SRC_ROOT/_catalog/_folders.txt" "$DEST/_catalog/"
    fi
    for system in ${SELECTED_SYSTEMS[@]+"${SELECTED_SYSTEMS[@]}"}; do
        if [ -f "$SRC_ROOT/_catalog/$system.txt" ]; then
            cp -f -- "$SRC_ROOT/_catalog/$system.txt" "$DEST/_catalog/"
            catalog_copied=$((catalog_copied + 1))
        fi
    done
fi

echo ""
echo "Done. Copied $copied artwork files (+ $catalog_copied catalog manifests) into $(cd "$DEST" && pwd)"
echo "Next step: copy the contents of '$DEST' onto your SD card's /pixelcade folder,"
echo "so paths look like /pixelcade/mame/pacman.png on the card."
