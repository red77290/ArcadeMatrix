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

Il scanne un niveau de sous-dossiers à l'intérieur du dossier `gifs/` de ta carte SD. Chaque
sous-dossier devient une "playlist" sélectionnable dans la Web UI. Par exemple :

```text
gifs/
  ├── mario.gif          <- toujours joué, pas une playlist (fichier isolé à la racine de gifs/)
  ├── mario/
  │   ├── walk.gif
  │   └── jump.gif
  └── sonic/
      └── run.gif
```

Ici, `mario/` et `sonic/` deviennent deux playlists activables/désactivables depuis la Web UI.

## Utilisation

Lance le script en pointant soit vers la **racine** de ta carte SD, soit directement vers son
**dossier `gifs/`** - les deux fonctionnent, le script détecte automatiquement lequel tu lui as
donné :

```bash
# macOS/Linux
./generate_index.sh /Volumes/SDCARD          # racine SD - descend automatiquement dans gifs/
./generate_index.sh /Volumes/SDCARD/gifs     # ou le dossier gifs/ directement
```

```powershell
# Windows
.\generate_index.ps1 -Path E:\               # racine SD - descend automatiquement dans gifs\
.\generate_index.ps1 -Path E:\gifs           # ou le dossier gifs\ directement
```

Le script écrit toujours le résultat dans **`<carte_sd>/gifs/playlists.json`**, le chemin exact
attendu par le firmware (`WebServerAPI.cpp` sert `/api/playlists` en lisant
`/gifs/playlists.json` sur la carte SD). Si ce fichier est absent ou obsolète, le sélecteur de
playlists de la Web UI n'affichera simplement rien à choisir (la lecture des GIFs n'est pas
affectée pour autant).

**Relance le script à chaque fois que tu ajoutes, supprimes ou renommes un dossier dans
`gifs/`** pour que la Web UI reste synchronisée avec ce qui se trouve réellement sur la carte SD.

---
*Cet outil est open source et conçu pour l'écosystème ArcadeMatrix.*
