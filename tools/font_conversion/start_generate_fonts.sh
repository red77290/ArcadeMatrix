#!/bin/bash
echo "==================================="
echo "ArcadeMatrix Font Converter (BDF -> AMF)"
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
read -p "SD Card root or fonts folder (e.g. /Volumes/SDCARD or /Volumes/SDCARD/fonts): " sd_path

echo ""
python generate_fonts.py "$sd_path"

echo ""
echo "[DONE]"
