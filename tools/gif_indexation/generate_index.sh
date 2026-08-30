#!/usr/bin/env bash
# generate_index.sh
# Scans subdirectories of your SD card's /gifs/ (YOKO) and /gifs_tate/ (TATE) folders
# for .gif and .raw files and generates the playlists.json manifest the ESP32 Web UI needs.
# Usage: ./generate_index.sh <path_to_sd_card_root_or_gifs_folder>

if [ -z "$1" ]; then
    echo "Usage: ./generate_index.sh <path_to_sd_card_root_or_gifs_folder>"
    echo "  Pass your SD card root (both 'gifs' and 'gifs_tate' will be indexed)"
    echo "  or a specific gifs/gifs_tate folder."
    exit 1
fi

TARGET_INPUT="$1"

index_folder() {
    local ROOT_DIR="$1"
    local FOLDER_BASE="$(basename "$ROOT_DIR")"
    local OUT_FILE="$ROOT_DIR/playlists.json"

    if [ ! -d "$ROOT_DIR" ]; then
        return
    fi

    echo "--- Indexing $FOLDER_BASE ($ROOT_DIR) ---"
    echo "{" > "$OUT_FILE"

    first_dir=true
    find "$ROOT_DIR" -mindepth 1 -maxdepth 1 -type d | while read -r dir; do
        folder_name="${dir##*/}"
        
        # Skip hidden directories like .Trashes, .Spotlight-V100, etc.
        if [[ "$folder_name" == .* ]]; then
            continue
        fi
        
        files=()
        shopt -s nullglob
        for ext in gif raw GIF RAW; do
            for f in "$dir"/*.$ext; do
                if [ -f "$f" ]; then
                    base_f="${f##*/}"
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
            echo "    \"path\": \"/$FOLDER_BASE/$folder_name\"," >> "$OUT_FILE"
            echo "    \"count\": ${#files[@]}" >> "$OUT_FILE"
            printf "  }" >> "$OUT_FILE"
            
            # Create index.txt inside the folder for O(1) random access in GifEngine
            INDEX_TXT="$dir/index.txt"
            printf "%s\n" "${files[@]}" > "$INDEX_TXT"
            
            echo "  [OK] Found ${#files[@]} animations in $folder_name"
        fi
    done

    echo "" >> "$OUT_FILE"
    echo "}" >> "$OUT_FILE"
    echo "[OK] Successfully created $OUT_FILE"
}

if [ -d "$TARGET_INPUT/gifs" ] || [ -d "$TARGET_INPUT/gifs_tate" ]; then
    index_folder "$TARGET_INPUT/gifs"
    index_folder "$TARGET_INPUT/gifs_tate"
else
    index_folder "$TARGET_INPUT"
fi

echo "Done! All GIF folders indexed successfully."
