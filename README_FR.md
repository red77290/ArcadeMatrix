# ArcadeMatrix

🇬🇧 [English](README.md) | 🇫🇷 Français | 🇪🇸 [Español](README_ES.md)

📺 **Démo Vidéo / Présentation :** https://youtu.be/2sA5wLVozRQ?si=T1gn6MYDwpq2-54c

Bienvenue sur le firmware open source ESP32 conçu pour piloter des matrices LED HUB75 ! Ce projet vous permet d'afficher des horloges Arcade, des GIF animés, la météo, et même des **sprites de jeux de combat MUGEN** simulés directement sur une vraie matrice LED.

---

> [!IMPORTANT]
> ### ⚡ Installation Rapide via Navigateur Web (Web Installer)
> Flashez votre carte ESP32 directement depuis votre navigateur (Chrome / Edge / Opera) en un clic sans aucun logiciel à installer !
> 
> 👉 **[🚀 Lancer ArcadeMatrix Web Installer](https://red77290.github.io/ArcadeMatrix/)**
> 
> | Version Firmware | Carte Matérielle Compatible | Bouton Web Installer |
> | :--- | :--- | :--- |
> | **ESP32-DevKit (Classique)** | ESP32-DevKitC, NodeMCU-32S, WROOM-32 (4MB Flash) | Sélectionnez **ESP32 (Standard)** |
> | **ESP32-S3 Waveshare** | Waveshare ESP32-S3 Matrix Board (32MB Flash + 16MB PSRAM (N32R16)) | Sélectionnez **ESP32-S3 (Waveshare)** |

---

## 💾 Releases & Carte SD

**[⬇️ Télécharger la dernière Release précompilée & Kit Carte SD](https://github.com/red77290/ArcadeMatrix/releases/latest)**
- **Archives Firmware** : Choisissez `ArcadeMatrix-esp32dev.zip` ou `ArcadeMatrix-esp32s3_waveshare.zip` selon votre carte (contient `firmware-*.bin`, `bootloader-*.bin`, `partitions-*.bin` et `boot_app0.bin` pour le flash manuel avec `esptool.py` - voir [Premiers pas](docs/GETTING_STARTED_FR.md#flashing-a-pre-built-release)).
- **Kit Carte SD (`ArcadeMatrix-sdcard.zip`)** : Contient l'arborescence complète à copier à la racine de la carte SD (`config.json`, dossiers GIFs/MUGEN, et scripts d'indexation).


- **🎛️ Master Desk Deck & Hub Multi-Widgets (`dashboard`) :** Horloge de bureau complète & dashboard avec cadrans pixel-art soignés, balayage fluide de trotteuse, horloges multi-fuseaux horaires mondiaux, météo extérieure (OpenWeatherMap + fallback gratuit Open-Meteo), climat intérieur calibré SHTC3, bandeau ticker live cryptos Binance & actions Yahoo Finance, et agencement auto-adaptatif dynamique 100% responsive !
- **Large sélection d'horloges animées (`clock`) :** horloges interactives incluant les classiques Arcade, Binary, Cyberpunk, Flip, Word, **Pac-Man**, **Tetris**, **SlotMachine**, **Pong**, **MatrixRain (Katakana)** et **Versus (Mugen)** !
- **📻 WebRadio Autonome & Moteur Musical (`music`) :** Streaming audio d'arrière-plan avec décodage MP3 linéaire temps réel (`minimp3`), sortie haute fidélité DAC I2S Everest ES8311, Bluetooth A2DP Sink, pochettes d'albums PNG couleur, artiste/titre défilant et visualiseur audio FFT 64 points Cooley-Tukey dynamique !
- **🧭 Auto-Rotation d'Écran Gyroscopique 6-Axes (`QMI8658` / `GyroHAL`) :** Orientation automatique de l'affichage ($0^\circ, 90^\circ, 180^\circ, 270^\circ$) par détection du vecteur de gravité physique, hystérèse anti-vibrations 500ms, offset mécanique de montage et calibration 1-clic depuis la Web UI !
- **🎵 Spotify Now Playing (`spotify`) :** affichage en direct du morceau en cours de lecture avec pochette d'album en couleur, défilement artiste/titre, barre de progression et égaliseur audio animé.
- **📡 Google Cast & Nest (`google_cast`) :** découverte automatique mDNS de vos enceintes Google Home / Nest Audio et affichage en direct des médias et flux audio diffusés.
- **🖥️ Moniteur Système (`sysinfo`) :** surveillance en direct de l'utilisation CPU (%), RAM (%), température SoC (°C/°F) et Uptime avec jauges colorées et thèmes visuels.
- **🥊 Moteur de combat M.U.G.E.N (`fighter`) :** combats de sprites rétro authentiques (Street Fighter, KOF, DBZ, Marvel...) extraits directement en RGB565 sans saccade, jouables en mode autonome ou en overlay discret sur vos horloges.
- **📈 Tickers & Graphiques Crypto / Bourse (`crypto`, `stock`) :** cotations en direct, variations % sur 24h et courbes sparklines historiques depuis CoinGecko, Binance et Yahoo Finance avec cache intelligent.
- **🌦️ Météo dynamique (`weather`) :** météo en direct, température actuelle, prévisions sur 3 jours et icônes rétro animées via OpenWeatherMap.
- **🌡️ Température & Humidité Intérieure (SHTC3) :** affichage dynamique (°C/°F), icônes Pixel Art thermomètre/eau, et endpoint REST pour remonter les données dans Home Assistant !
- **🔊 Sonomètre & Décibelomètre (Gaming Room / Arcade) :** mesure en temps réel du volume sonore ambiant en dB SPL avec 6 smileys Pixel Art réactifs (<45dB 😊 à >88dB 🚨) et Visualiseur Audio. ([🎥 Voir la Démo](https://youtu.be/Ljx5W2vFIU8?si=efGPixHGv7h8kcQU))
- **🎵 Visualiseur de Musique Rythmique :** 4 modes d'affichage prioritaire (Equalizer Spectrum avec peak hold, Oscilloscope Waveform, Radial Circles et Neon Fire).
- **Interface Web Wi-Fi :** accédez à `http://arcadematrix.local` pour envoyer des GIF, calibrer l'orientation d'écran et modifier la configuration en direct !
- **Moteur GIF (`gifs`) :** lecture fluide des GIF et playlists organisées sur la carte SD.
- **Support MQTT (`marquee`) :** s'intègre parfaitement avec Batocera et Recalbox pour afficher les marquees de jeux officiels via votre fork Pixelcade.
- **Mises à jour OTA :** Flashez les mises à jour du firmware sans fil directement via l'interface Web ou le Web Installer.
- **Support ESP32-S3 Waveshare :** Support complet des cartes ESP32-S3 haut de gamme et des dalles 256x64 True Matrix via DMA.

## Structure de la carte SD
Formatez votre carte SD en **FAT32** ou **exFAT**. Votre carte SD doit ressembler à ceci :
```
SD:/
  ├─ config.json
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

## Configuration (`config.json`)
Le fichier `config.json` situé à la racine de votre carte SD est exhaustif. Il contient les paramètres liés à la taille de la matrice, à la profondeur de couleur, aux thèmes d'horloge, à l'ordre de rotation au repos et aux arrière-plans des sprites MUGEN.
Ouvrez le `config.json` fourni dans le dossier `release/sdCard/` pour voir toutes les valeurs possibles.

## Extraction des sprites MUGEN (script `mugen_extractor.py`)
Pour afficher des combattants dans le module `SPRITES`, l'ESP32 attend des fichiers bruts `.fgt`. Comme l'ESP32 n'est pas assez puissant pour décoder nativement les formats complexes de personnages MUGEN, nous fournissons un script Python sur mesure pour les convertir et générer un manifeste `index.txt` contenant des boîtes englobantes parfaites et les valeurs de sol virtuel.

### Comment utiliser l'extracteur :
1. Assurez-vous d'avoir Python 3 installé avec la bibliothèque `Pillow` (`pip install Pillow`), ou lancez simplement `tools/mugen_extractor/start_extractor.sh`/`.bat` qui s'en charge automatiquement pour vous.
2. Rendez-vous dans le dossier `tools/mugen_extractor/` du dépôt.
3. Lancez le script en pointant `--src` vers votre dossier MUGEN `chars/` :
   ```bash
   python mugen_extractor.py --src /Chemin/Vers/Vos/Personnages/Mugen/chars --dest ./fighters_32
   # Ou avec un facteur d'échelle personnalisé (ex: --scale 0.5 pour diviser par 2 la taille des sprites et économiser 75% de RAM) :
   python mugen_extractor.py --src /Chemin/Vers/Vos/Personnages/Mugen/chars --dest ./fighters_64 --scale 0.5
   ```
4. Le script génère les fichiers `.fgt` ainsi qu'un manifeste `index.txt`/`index.json` dans le dossier `--dest`. Lancez-le deux fois (avec `--dest ./fighters_32` puis `--dest ./fighters_64`) si vous voulez des assets pour les deux tailles de matrice.
5. Copiez le dossier `fighters_32/` ou `fighters_64/` obtenu sur votre carte SD.

Pour tous les détails, consultez la documentation dans `tools/mugen_extractor/README_FR.md`.

### Arrière-plans des sprites
Les combattants ont besoin d'une arène ! Vous pouvez définir l'arrière-plan sur lequel ils se battent en plaçant un fichier image brut (par ex. `stage1.raw`) dans `SD:/fighters_32/backgrounds/`.
Ensuite, associez cet arrière-plan dans votre `config.json` sous la section `[DATE]` (les arrière-plans servent à enrichir le module date !) :
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

## ⚡ Compatibilité Matérielle & Fonctionnalités

| Fonctionnalité | ESP32-S3 (Carte Waveshare) | ESP32 Classique (DevKit) |
| :--- | :---: | :---: |
| Taille de matrice | Jusqu'à 256x64 (Vraie Matrice) | Jusqu'à 128x32 |
| Double Buffering | ✅ Oui (Fluide) | ✅ Oui (Fluide) |
| Animations (GIFs) | ✅ Oui | ✅ Oui |
| Moteur MUGEN | ✅ Oui | ✅ Oui |
| Interface Web & Wi-Fi | ✅ Oui | ✅ Oui |
| **WebRadio Autonome & Décodage MP3** | ✅ Oui (DAC ES8311 & Ampli intégrés) | ❌ Non (Nécessite DAC I2S & PSRAM) |
| **Bluetooth Audio A2DP Sink** | ✅ Oui (Stack A2DP Sink ESP-IDF) | ⚠️ Limité (Nécessite un DAC externe) |
| **Auto-Rotation Gyroscope 6-Axes (`QMI8658`)** | ✅ Oui (IMU intégré & Calibrate 1-clic) | ❌ Non (Nécessite capteur I2C externe) |
| **Crypto en Temps Réel** | ✅ Oui | ❌ Non (Manque de RAM pour le SSL) |
| **Bourse** | ✅ Oui | ❌ Non (Manque de RAM pour le SSL) |
| **Décibelmètre** | ✅ Oui (Double Micro ES7210 intégré) | ❌ Non (Nécessite un micro I2S externe & du code personnalisé) |
| **Température & Humidité (SHTC3)** | ✅ Oui (Capteur Intégré) | ❌ Non (Nécessite un SHTC3 I2C externe & du code personnalisé) |

> [!NOTE]
> **Détection Matérielle Dynamique & Dégradation Douce :** Tous les capteurs matériels (Gyroscope `QMI8658`, Microphone `ES7210`, DAC `ES8311`, Capteur de température `SHTC3`) sont sondés dynamiquement au démarrage sur le bus I2C/I2S. Si un composant est absent de votre carte, la fonctionnalité est **automatiquement désactivée sans aucun plantage**, avec repli sur le pilotage manuel via l'interface Web.

- **Carte ESP32-S3 Waveshare RGB Matrix (`esp32s3_waveshare`)** : **100% compatible avec toutes les fonctionnalités.** Fortement recommandée. Indispensable pour les grands panneaux **256x64**, le streaming audio WebRadio, l'auto-rotation gyroscopique, les modules gourmands en RAM (Crypto, Bourse), et exploite les capteurs matériels intégrés (Décibelmètre, Température, DAC HP) directement.
- **ESP32 Classique (WROOM-32 / `esp32dev`)** : Processeur double cœur Tensilica Xtensa LX6 @ 240MHz. Supporte les animations de base, l'interface Web et MUGEN pour les matrices **128x32 / 64x32**. Ne supporte pas les fonctionnalités lourdes en RAM (HTTPS/SSL, streaming audio autonome). Les capteurs intégrés sont également absents.

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

## 🙏 Remerciements

Un immense merci à la communauté open source et aux créateurs des formidables bibliothèques qui font tourner ce projet :
- **[ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA)** par mrfaptastic
- **[AnimatedGIF](https://github.com/bitbank2/AnimatedGIF)** & **[PNGdec](https://github.com/bitbank2/PNGdec)** par bitbank2
- **[ESPAsyncWebServer](https://github.com/mathieucarbou/ESPAsyncWebServer)** par mathieucarbou
- **[ArduinoJson](https://github.com/bblanchon/ArduinoJson)** par bblanchon
- **[PubSubClient](https://github.com/knolleary/pubsubclient)** par knolleary
- **[PicoMQTT](https://github.com/mlesniew/PicoMQTT)** par mlesniew
- **[Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library)** par Adafruit
- **[SdFat](https://github.com/greiman/SdFat)** par greiman

Un grand merci à la **RPiTeam** pour le super pack de 600 GIFs !

## 📜 Licence
Ce projet est publié sous la **[PolyForm Noncommercial License 1.0.0](LICENSE)**.

**En résumé :** vous êtes libre d'utiliser, modifier et partager ce projet pour tout usage non-commercial (usage personnel, projet hobbyiste, recherche, éducation, organismes publics/à but non lucratif) - voir le fichier [LICENSE](LICENSE) complet pour les termes exacts. **Tout usage commercial (vente d'unités assemblées, de kits, ou de produits/services dérivés) nécessite une licence séparée - contactez [Red1L](https://github.com/red77290) pour discuter des conditions commerciales.**
