#!/bin/bash
echo "==================================="
echo "ArcadeMatrix MUGEN / Sprite Extractor"
echo "==================================="

# Go to script directory
cd "$(dirname "$0")" || exit

if [ ! -d "venv" ]; then
    echo "[INFO] Creating Python Virtual Environment..."
    python3 -m venv venv
    if [ $? -ne 0 ]; then
        echo "[ERROR] python3 is not installed or not in PATH!"
        exit 1
    fi
fi

echo "[INFO] Activating virtual environment..."
source venv/bin/activate

echo "[INFO] Installing requirements..."
pip install -r requirements.txt -q

echo ""
read -p "Input Folder (containing source GIFs): " input_folder
read -p "Output Folder (e.g. ./output/ryu): " output_folder

echo ""
python mugen_extractor.py -i "$input_folder" -o "$output_folder"

echo ""
echo "[DONE]"
