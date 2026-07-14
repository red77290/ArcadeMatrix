# ArcadeMatrix

Bienvenue sur le firmware open-source ESP32 conçu pour piloter des Matrices LED HUB75 ! Ce projet vous permet d'afficher des Horloges d'Arcade, des GIFs animés, la Météo, et même de simuler des **combats de sprites du jeu MUGEN** directement sur une vraie matrice LED.

📚 **Liens de documentation :**
- [Guide Matériel](docs/HARDWARE.md)
- [Guide de Câblage](docs/WIRING.md)
- [Guide de Configuration](docs/CONFIGURATION.md)

## Fonctionnalités
- **Sélection massive d'horloges :** Des horloges animées comprenant les classiques Arcade, Binaire, Cyberpunk, Flip, Mots, **Pac-Man**, **Tetris**, **SlotMachine**, et **Versus (Mugen)** !
- **Interface Web Wi-Fi :** Accédez à `http://arcadematrix.local` pour uploader des GIFs et modifier les paramètres en direct !
- **Moteur MUGEN Fighter :** Simule des jeux de combat 2D nativement sur la matrice en utilisant des sprites extraits, avec un alignement parfait au "sol virtuel" (virtual ground).
- **Moteur GIF :** Lecture fluide de GIFs stockés sur la carte SD.
- **Support MQTT :** S'intègre parfaitement avec Batocera et Recalbox pour afficher les "marques" (bannières) des jeux.

## Structure de la Carte SD
Formatez votre carte SD en **FAT32**. Votre carte SD doit ressembler à ceci :
```
SD:/
  ├─ conf.ini
  ├─ playlists.json
  ├─ gifs/
  │   └─ mario.gif
  └─ fighters_32/
      ├─ backgrounds/
      │   └─ stage1.raw
      └─ ryu/
          ├─ idle.fgt
          └─ attack.fgt
  └─ fighters_64/
      └─ (même structure pour les panneaux de 64px de hauteur)
```
*Note : Le dossier `www/` n'est plus requis sur la carte SD car l'interface Web est désormais directement intégrée au firmware de l'ESP32 !*

## Configuration (`conf.ini`)
Le fichier `conf.ini` situé à la racine de votre carte SD est exhaustif. Il contient les paramètres pour la taille de la Matrice, la profondeur des couleurs, les thèmes d'horloge, l'ordre de rotation au repos, et les arrière-plans des sprites MUGEN.
Ouvrez le `conf.ini` fourni dans le dossier `release/sdcard/` pour voir toutes les valeurs possibles.

## Extraction des Sprites MUGEN (Le Script `mugen_extractor.py`)
Pour afficher des combattants dans le module `SPRITES`, l'ESP32 a besoin de fichiers bruts `.fgt`. Étant donné que l'ESP32 n'est pas assez puissant pour décoder les formats complexes des personnages MUGEN nativement, nous fournissons un script Python sur mesure pour les convertir et générer un manifeste `index.txt` contenant les boîtes de collision (bounding boxes) parfaites et les valeurs de sol virtuel.

### Comment utiliser l'extracteur :
1. Assurez-vous d'avoir installé Python 3 avec la bibliothèque `Pillow` (`pip install Pillow`).
2. Allez dans le dossier `tools/mugen_extractor/` du dépôt.
3. Éditez le fichier `mugen_extractor.py` pour configurer `src_dir` afin de pointer vers votre dossier `chars/` MUGEN.
4. Lancez le script :
   ```bash
   python mugen_extractor.py
   ```
5. Le script génèrera automatiquement les fichiers `.fgt` ainsi que le manifeste `index.txt` pour tous les personnages, parfaitement mis à l'échelle pour les matrices 32px et 64px.
6. Copiez le dossier résultant `fighters_32/` ou `fighters_64/` sur votre carte SD.

Pour plus de détails, veuillez lire la documentation dans `tools/mugen_extractor/README_FR.md`.

### Arrière-plans des Sprites
Les combattants ont besoin d'une arène ! Vous pouvez définir l'arrière-plan sur lequel ils se battent en plaçant un fichier image brut (ex: `stage1.raw`) dans `SD:/fighters_32/backgrounds/`.
Ensuite, liez cet arrière-plan dans votre `conf.ini` sous la section `[DATE]` (les arrière-plans sont utilisés pour pimenter le module de la date !) :
```ini
BACKGROUND_SPRITE=stage1.raw
```

## Compilation
Pour compiler le firmware vous-même, vous devez utiliser **PlatformIO**.
- Pour 128x32 : Un ESP32 WROOM standard est suffisant.
- Pour 256x64 : Un **ESP32-S3 avec PSRAM** est fortement recommandé pour éviter les crashs de manque de mémoire (Out-Of-Memory) avec le double buffering.

Lancez la commande suivante pour compiler :
```bash
pio run -e esp32dev
```
