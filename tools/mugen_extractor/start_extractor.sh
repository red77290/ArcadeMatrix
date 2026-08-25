#!/bin/bash
echo "====================================================="
echo "   ArcadeMatrix MUGEN / Sprite Character Extractor   "
echo "====================================================="

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
# 1. Input Folder
default_input="./chars"
read -e -p "📁 Dossier Source MUGEN (contenant les personnages) [$default_input]: " input_folder
input_folder="${input_folder:-$default_input}"

# 2. Output Folder
default_output="./fighters_32"
read -e -p "📁 Dossier Destination (ex: ./fighters_32) [$default_output]: " output_folder
output_folder="${output_folder:-$default_output}"

# 3. Scaling / Échelle
echo ""
echo "-----------------------------------------------------"
echo "📏 Choix du Facteur d'Échelle (Scaling Factor) :"
echo "   • 0.5   : Recommandé ESP32 Matrice 128x32 / 64x32 (hauteur ~32px)"
echo "   • 1.0   : Taille 1:1 d'origine (Matrice 128x64, 256x64, RPi, ESP32-S3 PSRAM)"
echo "   • auto  : Ajustement automatique proportionnel à la hauteur de l'écran"
echo "   • Ou entrez une valeur personnalisée (ex: 0.4, 0.6, 0.75, 1.25)"
echo "-----------------------------------------------------"
read -p "Échelle souhaitée [défaut: 0.5]: " scale_input
scale_input="${scale_input:-0.5}"

# 4. Compression
echo ""
echo "-----------------------------------------------------"
echo "🗜️  Compression des fichiers (.fgt vs .fgt.gz) :"
echo "   • n (Non) : Recommandé pour ESP32 / SD-Card / LittleFS (décodage rapide)"
echo "   • y (Oui) : Recommandé pour Raspberry Pi / Stockage limité (-80% d'espace)"
echo "-----------------------------------------------------"
read -p "Compresser en .fgt.gz ? (y/n) [défaut: n]: " compress_input
compress_input="${compress_input:-n}"

# Build arguments
EXTRA_ARGS=""
if [ "$scale_input" = "auto" ] || [ "$scale_input" = "scaled" ] || [ "$scale_input" = "SCALED" ]; then
    EXTRA_ARGS="--mode SCALED"
elif [ "$scale_input" = "1.0" ] || [ "$scale_input" = "full" ] || [ "$scale_input" = "FULLSIZE" ]; then
    EXTRA_ARGS="--scale 1.0"
else
    EXTRA_ARGS="--scale $scale_input"
fi

if [ "$compress_input" = "y" ] || [ "$compress_input" = "Y" ] || [ "$compress_input" = "yes" ]; then
    EXTRA_ARGS="$EXTRA_ARGS --compress"
fi

echo ""
echo "🚀 Lancement de l'extraction :"
echo "   • Source      : $input_folder"
echo "   • Destination : $output_folder"
echo "   • Paramètres  : $EXTRA_ARGS"
echo ""

python mugen_extractor.py -i "$input_folder" -o "$output_folder" $EXTRA_ARGS

echo ""
echo "✅ [TERMINÉ] Extraction terminée."
