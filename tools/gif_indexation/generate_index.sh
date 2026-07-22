#!/usr/bin/env bash
# generate_index.sh
# Scans subdirectories of your SD card's /gifs/ folder for .gif and .raw files
# and generates the playlists.json manifest the ESP32 Web UI needs to let you
# pick which folders to play (WebServerAPI.cpp reads it from /gifs/playlists.json).
# Usage: ./generate_index.sh <path_to_sd_card_root_or_gifs_folder>

if [ -z "$1" ]; then
    echo "Usage: ./generate_index.sh <path_to_sd_card_root_or_gifs_folder>"
    echo "  You can pass either the SD card root (a 'gifs' subfolder will be used"
    echo "  automatically) or the gifs folder itself."
    exit 1
fi

TARGET_DIR="$1"

# Accept either the SD root (auto-descend into ./gifs) or the gifs folder itself.
if [ "$(basename "$TARGET_DIR")" != "gifs" ] && [ -d "$TARGET_DIR/gifs" ]; then
    TARGET_DIR="$TARGET_DIR/gifs"
fi

if [ ! -d "$TARGET_DIR" ]; then
    echo "[ERROR] '$TARGET_DIR' does not exist. Pass your SD card root or its gifs/ folder."
    exit 1
fi

ROOT_DIR="$TARGET_DIR"
OUT_FILE="$ROOT_DIR/playlists.json"

echo "{" > "$OUT_FILE"

# Iterate over directories
first_dir=true
find "$ROOT_DIR" -mindepth 1 -maxdepth 1 -type d | while read -r dir; do
    folder_name="${dir##*/}"
    
    # Skip hidden directories like .Trashes, .Spotlight-V100, etc.
    if [[ "$folder_name" == .* ]]; then
        continue
    fi
    
    # Get all animations in this dir
    files=()
    shopt -s nullglob
    for ext in gif raw GIF RAW; do
        for f in "$dir"/*.$ext; do
            if [ -f "$f" ]; then
                base_f="${f##*/}"
                # Skip macOS AppleDouble (._) files
                if [[ "$base_f" != ._* ]]; then
                    files+=("$base_f")
                fi
            fi
        done
    done
    shopt -u nullglob
    
    if [ ${#files[@]} -gt 0 ]; then
        if [ "$first_dir" = true ]; then
            first_dir=false
        else
            echo "," >> "$OUT_FILE"
        fi
        
        echo "  \"$folder_name\": {" >> "$OUT_FILE"
        echo "    \"path\": \"/gifs/$folder_name\"," >> "$OUT_FILE"
        echo "    \"count\": ${#files[@]}" >> "$OUT_FILE"
        printf "  }" >> "$OUT_FILE"
        
        # Also create an index.txt inside the folder for O(1) random access in GifEngine
        INDEX_TXT="$dir/index.txt"
        printf "%s\n" "${files[@]}" > "$INDEX_TXT"
        
        echo "[OK] Found ${#files[@]} animations in $folder_name"
    fi
done

echo "" >> "$OUT_FILE"
echo "}" >> "$OUT_FILE"

echo "Done! Successfully created $OUT_FILE"
