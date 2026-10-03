# pixelcade_sync

🇬🇧 [English](README.md) | 🇫🇷 Français | 🇪🇸 [Español](README_ES.md)

Outil one-shot côté PC qui télécharge le dépôt d'artworks marquee de [Pixelcade](https://github.com/alinke/pixelcade)
et l'organise de façon prête à copier sur votre carte SD ArcadeMatrix, afin que l'ESP32 n'ait jamais besoin d'un accès internet en direct pour afficher box-art / marquees pendant le jeu.

## Pourquoi ne pas le récupérer en direct depuis l'ESP32 ?

`ArcadeMatrix_RPi` (le projet frère sur Raspberry Pi) télécharge les images Pixelcade à la demande
(`core/dmd_cache.py`), puis les met en cache sur disque après le premier téléchargement. L'ESP32 ne peut pas raisonnablement faire la même chose :
- Aucun budget flash / RAM disponible pour un client HTTPS+TLS récupérant des images en pleine partie, en plus du driver DMA de la matrice.
- Aucun cache de système de fichiers suffisamment grand pour croître sans limite, ni logique d'éviction de cache qui justifierait cette complexité sur un microcontrôleur.

À la place, `FrontendSyncEngine` sur l'ESP32 s'attend à trouver l'artwork déjà présent sous
`/pixelcade/<system>/<game>.png` sur la carte SD — ce script remplit ce dossier une fois (hors ligne,
sur un vrai PC), vous le copiez sur la carte SD, puis le firmware se contente d'un `SD.exists()` rapide +
`gif->playGif()` à l'exécution. Aucun aller-retour réseau au démarrage du jeu.

## Utilisation

Pas de Python, pas d'install tierce, rien à télécharger — juste le script et les outils que votre OS
a déjà. Choisissez celui qui correspond à votre plateforme :

### macOS / Linux

```bash
# Sync every system Pixelcade has artwork for (large - several hundred MB)
./pixelcade_sync.sh

# Only sync the systems you actually use (recommended - much faster/smaller)
./pixelcade_sync.sh mame,snes,nes,gba

# Custom output location
DEST=./sdcard/pixelcade ./pixelcade_sync.sh mame
```

Nécessite seulement `curl` (ou `wget`) et `unzip`, déjà installés sur pratiquement toutes les machines
Mac/Linux. Si l'un des deux manque, le script vous indique exactement quoi installer et comment
(par ex. `brew install unzip curl` / `sudo apt install unzip curl`).

### Windows

```powershell
# Sync every system Pixelcade has artwork for (large - several hundred MB)
.\pixelcade_sync.ps1

# Only sync the systems you actually use (recommended - much faster/smaller)
.\pixelcade_sync.ps1 -Systems mame,snes,nes,gba

# Custom output location
.\pixelcade_sync.ps1 -Dest D:\sdcard\pixelcade -Systems mame
```

Nécessite uniquement ce qui est livré nativement avec Windows 10/11 (PowerShell 5+, `Invoke-WebRequest`,
`Expand-Archive`) — aucune installation nécessaire. Si votre PowerShell est trop ancien ou s'il manque un composant, le
script vous explique exactement ce qui manque et comment le corriger. Si l'execution policy le bloque,
lancez : `powershell -ExecutionPolicy Bypass -File .\pixelcade_sync.ps1`

## Application sur votre carte SD

Copiez le contenu du dossier de sortie à la racine de votre carte SD ArcadeMatrix, afin d'obtenir
par exemple :

```
/pixelcade/mame/pacman.png
/pixelcade/snes/super_mario_world.png
```

Les noms de dossiers de système (`mame`, `snes`, `nes`, ...) correspondent à la structure propre au dépôt Pixelcade, et
`FrontendSyncEngine::mapSystemToPixelcadeFolder()` (côté firmware) mappe les valeurs `SystemId` de Recalbox/Batocera
(par ex. `fbneo`, `megadrive`) vers ces mêmes noms de dossiers — maintenus synchronisés avec le `SYSTEM_MAP` de
`ArcadeMatrix_RPi/core/dmd_cache.py`. Si vous ajoutez un système à cet endroit, répercutez le changement dans les deux emplacements.

## Relancer / garder l'artwork à jour

Le dépôt Pixelcade s'enrichit au fil du temps à mesure que de nouveaux jeux reçoivent leur artwork. Il suffit de relancer ce script périodiquement (il retélécharge le snapshot complet à chaque fois — il n'y a pas de mode incrémental / diff) puis de recopier le résultat sur votre carte SD pour récupérer les nouveaux jeux.
