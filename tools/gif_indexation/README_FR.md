# Indexeur de Playlists GIF ArcadeMatrix

🇬🇧 [English](README.md) | 🇫🇷 Français | 🇪🇸 [Español](README_ES.md)

Cet outil génère le manifeste `playlists.json` dont l'interface Web de l'ESP32 a besoin pour te
permettre de cocher/décocher les sous-dossiers de `gifs/` ("playlists") à inclure dans la
rotation en mode idle. Il n'est **nécessaire que pour cette liste de sélection de la Web UI** -
la lecture des GIFs fonctionne sans lui, puisque le `GifEngine` lit toujours directement les
fichiers `.gif`/`.png`/`.raw` réels sur la carte SD au moment de l'exécution.

Deux scripts natifs sont fournis - **aucun Python requis** :
- `generate_index.sh` (macOS/Linux, Bash pur)
- `generate_index.ps1` (Windows, PowerShell pur)

## Ce qu'il fait

Il scanne les sous-dossiers situés dans les répertoires **`gifs/`** (Horizontal / YOKO) et **`gifs_tate/`** (Vertical / TATE) de votre carte SD :
- Chaque sous-dossier devient une playlist sélectionnable dans la Web UI.
- Génère un fichier `index.txt` à l'intérieur de chaque sous-dossier pour un accès aléatoire $O(1)$ ultra-rapide par le firmware.
- Génère `playlists.json` à la racine de `gifs/` et `gifs_tate/` avec le nombre d'animations.

## Utilisation

Lancez le script en pointant soit vers la **racine** de votre carte SD, soit vers un dossier spécifique :

```bash
# macOS/Linux
./generate_index.sh /Volumes/SDCARD          # Racine SD - indexe à la fois gifs/ (YOKO) et gifs_tate/ (TATE)
./generate_index.sh /Volumes/SDCARD/gifs     # ou le dossier gifs/ spécifiquement
./generate_index.sh /Volumes/SDCARD/gifs_tate# ou le dossier gifs_tate/ spécifiquement
```

```powershell
# Windows
.\generate_index.ps1 -Path E:\               # Racine SD - indexe à la fois gifs\ (YOKO) et gifs_tate\ (TATE)
.\generate_index.ps1 -Path E:\gifs           # ou le dossier gifs\ spécifiquement
.\generate_index.ps1 -Path E:\gifs_tate      # ou le dossier gifs_tate\ spécifiquement
```

Le script génère **`<carte_sd>/gifs/playlists.json`** et **`<carte_sd>/gifs_tate/playlists.json`**, ainsi que les fichiers **`index.txt`** dans chaque sous-dossier.

**Relancez le script à chaque fois que vous ajoutez, supprimez ou renommez des dossiers ou des GIFs** pour synchroniser l'interface Web et l'index du firmware.

---
*Cet outil est open source et conçu pour l'écosystème ArcadeMatrix.*
