# ArcadeMatrix

🇬🇧 [English](README.md) | 🇫🇷 Français | 🇪🇸 [Español](README_ES.md)

Bienvenue sur le firmware open source ESP32 conçu pour piloter des matrices LED HUB75 ! Ce projet vous permet d'afficher des horloges Arcade, des GIF animés, la météo, et même des **sprites de jeux de combat MUGEN** simulés directement sur une vraie matrice LED.

## 💾 Installation

**[⬇️ Télécharger le dernier firmware précompilé](https://github.com/red77290/ArcadeMatrix/releases/latest)**
(construit et testé automatiquement par la CI à chaque release taguée - choisissez `ArcadeMatrix-esp32dev.zip`
ou `ArcadeMatrix-esp32s3.zip` selon votre carte, puis flashez `firmware-*.bin`,
`bootloader-*.bin`, `partitions-*.bin` et `boot_app0.bin` avec `esptool.py` - voir
[Premiers pas](docs/GETTING_STARTED_FR.md#flashing-a-pre-built-release) pour les offsets exacts et la
commande. Le Web Installer via navigateur ci-dessus sera l'option la plus simple une fois le dépôt public.
Récupérez aussi `ArcadeMatrix-sdcard.zip` sur la même release - un kit de démarrage de carte SD
prêt à copier avec un exemple de `conf.ini`, les dossiers GIFs/MUGEN, et les scripts d'indexation
des playlists GIF.)


## Fonctionnalités
- **Large sélection d'horloges :** horloges animées incluant les classiques Arcade, Binary, Cyberpunk, Flip, Word, **Pac-Man**, **Tetris**, **SlotMachine**, **MatrixRain** et **Versus (Mugen)** !
- **Tickers Crypto & Bourse en temps réel :** cotations en direct et badges % sur 24h depuis CoinGecko, Binance et Yahoo Finance avec cache configurable.
- **Interface Web Wi-Fi :** accédez à `http://arcadematrix.local` pour envoyer des GIF et modifier la configuration en direct !
- **Moteur de combat MUGEN :** simule nativement des jeux de combat 2D sur la matrice à l'aide de sprites extraits avec un alignement parfait sur le sol virtuel.
- **Moteur GIF :** lecture fluide des GIF stockés sur la carte SD.
- **Météo (OpenWeatherMap) :** prévisions sur 3 jours, avec mise en cache de 15 minutes pour économiser vos appels d'API.
- **Support MQTT :** s'intègre parfaitement avec Batocera et Recalbox pour afficher les marquees des jeux.
- **Mises à jour OTA :** Flashez les mises à jour du firmware sans fil directement via l'interface Web.
 - **Support ESP32-S3 Waveshare :** Support complet des cartes ESP32-S3 haut de gamme et des dalles 256x64 True Matrix via DMA.

## Structure de la carte SD
Formatez votre carte SD en **FAT32** ou **exFAT**. Votre carte SD doit ressembler à ceci :
```
SD:/
  ├─ conf.ini
  ├─ gifs/
  │  │   └─ mario.gif
  └─ fighters_32/
      ├─ backgrounds/
      │   └─ stage1.raw
      └─ ryu/
          ├─ idle.fgt
          └─ attack.fgt
  └─ fighters_64/
      └─ (même structure pour les panneaux de 64px de haut)
```
*Remarque : le dossier `www/` n'est plus nécessaire sur la carte SD, car l'interface Web est désormais directement intégrée au firmware ESP32 !*

## Configuration (`conf.ini`)
Le fichier `conf.ini` situé à la racine de votre carte SD est exhaustif. Il contient les paramètres liés à la taille de la matrice, à la profondeur de couleur, aux thèmes d'horloge, à l'ordre de rotation au repos et aux arrière-plans des sprites MUGEN.
Ouvrez le `conf.ini` fourni dans le dossier `release/sdCard/` pour voir toutes les valeurs possibles.

## Extraction des sprites MUGEN (script `mugen_extractor.py`)
Pour afficher des combattants dans le module `SPRITES`, l'ESP32 attend des fichiers bruts `.fgt`. Comme l'ESP32 n'est pas assez puissant pour décoder nativement les formats complexes de personnages MUGEN, nous fournissons un script Python sur mesure pour les convertir et générer un manifeste `index.txt` contenant des boîtes englobantes parfaites et les valeurs de sol virtuel.

### Comment utiliser l'extracteur :
1. Assurez-vous d'avoir Python 3 installé avec la bibliothèque `Pillow` (`pip install Pillow`), ou lancez simplement `tools/mugen_extractor/start_extractor.sh`/`.bat` qui s'en charge automatiquement pour vous.
2. Rendez-vous dans le dossier `tools/mugen_extractor/` du dépôt.
3. Lancez le script en pointant `--src` vers votre dossier MUGEN `chars/` :
   ```bash
   python mugen_extractor.py --src /Chemin/Vers/Vos/Personnages/Mugen/chars --dest ./fighters_32
   ```
4. Le script génère les fichiers `.fgt` ainsi qu'un manifeste `index.txt`/`index.json` dans le dossier `--dest`. Lancez-le deux fois (avec `--dest ./fighters_32` puis `--dest ./fighters_64`) si vous voulez des assets pour les deux tailles de matrice.
5. Copiez le dossier `fighters_32/` ou `fighters_64/` obtenu sur votre carte SD.

Pour tous les détails, consultez la documentation dans `tools/mugen_extractor/README_FR.md`.

### Arrière-plans des sprites
Les combattants ont besoin d'une arène ! Vous pouvez définir l'arrière-plan sur lequel ils se battent en plaçant un fichier image brut (par ex. `stage1.raw`) dans `SD:/fighters_32/backgrounds/`.
Ensuite, associez cet arrière-plan dans votre `conf.ini` sous la section `[DATE]` (les arrière-plans servent à enrichir le module date !) :
```ini
BACKGROUND_SPRITE=stage1.raw
```

## Playlists GIF (Découverte Automatique)
Le firmware ESP32 scanne désormais dynamiquement votre carte SD et le dossier `/gifs/` à la volée. Vous n'avez plus besoin d'exécuter de scripts d'indexation ni de maintenir de fichier `playlists.json` !

1. Organisez simplement vos GIF dans des sous-dossiers sous `gifs/` sur votre carte SD, par ex. `gifs/mario/`, `gifs/sonic/`.
2. L'interface Web les détectera automatiquement comme des playlists sélectionnables.
3. Les fichiers `.gif` isolés placés directement à la racine de `gifs/` sont toujours joués par défaut.

## Polices personnalisées (conversion BDF → AMF)
L'Horloge, la Date et le message défilant peuvent utiliser des polices bitmap personnalisées chargées depuis la carte SD à la place des ~6 polices compilées dans le firmware, en utilisant les mêmes polices `.bdf` qu'`ArcadeMatrix_RPi` fournit déjà. L'ESP32 n'a cependant aucun parseur BDF embarqué, elles doivent donc d'abord être converties au format compact `.amf`.

1. Copiez votre/vos police(s) `.bdf` dans le dossier `fonts/` de votre carte SD.
2. Lancez le convertisseur en lot :
   ```bash
   python3 tools/bdf_to_amfont/bdf_to_amfont.py /Volumes/SDCARD   # passez la racine SD ou son dossier fonts/
   ```
   (Aucune dépendance externe requise. Python standard uniquement.)
3. Cela convertit chaque `.bdf` en un `.amf` de même nom, sur place. Les polices résultantes apparaissent immédiatement dans la page Settings de l'interface Web (menus déroulants "Font" Horloge/Date) - sans redémarrage nécessaire.

Pour tous les détails, consultez `tools/bdf_to_amfont/README_FR.md`.

## Compilation
Pour compiler le firmware vous-même, vous devez utiliser **PlatformIO**.
- Pour 128x32 : un ESP32 WROOM standard suffit.
- Pour 256x64 : un **ESP32-S3 avec PSRAM** est fortement recommandé pour éviter les crashs par manque de mémoire avec le double buffering.

Exécutez la commande suivante pour compiler :
```bash
pio run -e esp32dev
```

## 📚 Documentation complémentaire
- [Premiers pas (installation de PlatformIO, compilation, flash, logs)](docs/GETTING_STARTED_FR.md)
- [Web Installer (flash depuis votre navigateur, sans CLI)](webinstaller/README_FR.md) - *sera mis en ligne une fois ce dépôt public (GitHub Pages nécessite un dépôt public avec l'offre gratuite) ; en attendant, utilisez le firmware précompilé ci-dessus.*
- [Guide matériel](docs/HARDWARE_FR.md)
- [Guide de câblage](docs/WIRING_FR.md)
- [Guide de configuration](docs/CONFIGURATION_FR.md)
- [Guide développeur](docs/DEVELOPER_FR.md)
- [Architecture](docs/ARCHITECTURE_FR.md)

## 📜 Licence
Ce projet est publié sous la **[PolyForm Noncommercial License 1.0.0](LICENSE)**.

**En résumé :** vous êtes libre d'utiliser, modifier et partager ce projet pour tout usage non-commercial (usage personnel, projet hobbyiste, recherche, éducation, organismes publics/à but non lucratif) - voir le fichier [LICENSE](LICENSE) complet pour les termes exacts. **Tout usage commercial (vente d'unités assemblées, de kits, ou de produits/services dérivés) nécessite une licence séparée - contactez [Red1L](https://github.com/red77290) pour discuter des conditions commerciales.**
