# Pipeline de Pré-traitement des Assets ArcadeMatrix

Ce document décrit le pipeline d'optimisation hors-ligne des assets pour ArcadeMatrix ESP32.

---

## 💡 Pourquoi pré-traiter les assets hors-ligne ?

L'ESP32 est un microcontrôleur haute performance, mais sa mémoire RAM et sa fréquence d'horloge sont limitées par rapport à un processeur de PC ou de Raspberry Pi. 

Plutôt que de faire décoder à l'ESP32 des formats d'images lourds ou des scripts d'animations complexes en temps réel, ArcadeMatrix s'appuie sur une philosophie stricte : **Pré-traiter hors-ligne sur PC, lire ultra-rapidement en streaming sur l'ESP32**.

---

## 🕹️ 1. Sprites MUGEN (`.fgt`)

Le script Python `tools/mugen_extractor/mugen_extractor.py` convertit les personnages MUGEN (`.sff`, `.air`) en fichiers binaire `.fgt` optimisés.

- **Extraction** : Décodage de la palette maître et sélection des animations clés (`walk`, `attack`, `hit`, `win`, `special1-3`, `super1-3`, `fall`).
- **Sol Virtuel (Virtual Ground)** : Calcul d'une ligne de sol uniforme (`ground_y`) pour éviter tout tremblement d'alignement lors des coups.
- **Scaling (`--scale`)** : Ajustement de l'échelle (ex: `--scale 0.5` pour diviser la taille des sprites par 2 et économiser 75% de mémoire RAM sur la carte).
- **Indexation** : Génération de `index.txt` (utilisé par `FighterEngine.cpp` sur ESP32) et `index.json` (utilisé par Raspberry Pi).

```bash
# Exemple de conversion pour dalles 64px avec scaling 0.5 :
python3 tools/mugen_extractor/mugen_extractor.py --src /chemin/mugen/chars --dest /Volumes/SDCARD/fighters_64 --scale 0.5
```
---

## 🔤 2. Polices Bitmap Custom (`.amf`)

Le script `tools/bdf_to_amfont/bdf_to_amfont.py` convertit des polices bitmap au format standard `.bdf` vers le format binaire compact `.amf` (ArcadeMatrix Font).

- **Gain de performances** : Décodage binaire instantané sans parseur BDF textuel en mémoire.
- **Utilisation** : Droppez vos fichiers `.bdf` dans `/fonts/` sur la carte SD et exécutez le script. Les polices `.amf` générées apparaissent immédiatement dans la WebUI (menus déroulants Font pour Horloge et Date).

```bash
python3 tools/bdf_to_amfont/bdf_to_amfont.py /Volumes/SDCARD
```

---

## 🎬 3. Playlists & Animations GIF

- **Découverte Automatique** : Le firmware scanne dynamiquement `/gifs/` et ses sous-dossiers (ex: `/gifs/mario`, `/gifs/sonic`).
- **Indexation** : Les scripts `generate_index.sh` ou `generate_index.bat` créent un fichier `index.txt` dans chaque dossier GIF pour accélérer le tirage aléatoire et ignorer les fichiers macOS indésirables (`._*`).

---

## 🖼️ 4. Marquees Brutes RGB565 (`.raw`)

- **Format** : Píxels bruts en RGB565 little-endian (exactement `largeur * hauteur * 2` octets).
- **Utilisation** : Affichage d'arrière-plans fixes de combat (`SD:/fighters_32/backgrounds/stage1.raw`) ou poussés via l'API REST `POST /api/marquee`.
