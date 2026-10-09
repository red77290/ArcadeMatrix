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


## 🕹️ Moteurs Intégrés & Simulations Matérielles

Chaque moteur d'ArcadeMatrix est conçu avec **zéro allocation dynamique et zéro contention de mutex** sur le chemin critique Core 1, garantissant une cadence d'affichage absolue de 60 FPS. Voici les simulations fidèles de chaque moteur en fonctionnement sur dalles physiques HUB75 :

| Moteur / ID | Simulation Matérielle | Description & Fonctionnalités Clés |
| :--- | :---: | :--- |
| **Desk Master Dashboard**<br>`dashboard` | <img src="docs/assets/engines/engine_dashboard.png" width="240" alt="Moteur Dashboard"> | Horloge de bureau complète avec cadran analogique pixel-art, balayage fluide de trotteuse, horloges mondiales, climat intérieur SHTC3, et bandeau ticker live cryptos Binance & actions Yahoo Finance. |
| **Horloge Rétro Gaming**<br>`clock` | <img src="docs/assets/engines/engine_clock.png" width="240" alt="Horloge Rétro"><br><br>[👉 **Voir la galerie des 10+ horloges rétro ➔**](#-horloges-rétro-gaming-légendaires-posters--simulations-hub75) | Collection exclusive d'horloges animées d'arcade et de consoles rétro (Metal Slug, Castlevania, Mario, Mega Man, Sonic, Pokédex, Pac-Man, Tetris, Horloge Mondiale, Pluie Matrix...) avec sprites 100 % authentiques au pixel près. |
| **WebRadio & Lecteur Musical**<br>`music` | <img src="docs/assets/engines/engine_music.png" width="240" alt="Moteur Musique"> | Streaming audio autonome avec décodage MP3 linéaire temps réel (`minimp3`), sortie audio DAC I2S Everest ES8311, pochettes d'albums et visualiseur audio dynamique 64 bandes. |
| **Spotify Now Playing**<br>`spotify` | <img src="docs/assets/engines/engine_spotify.png" width="240" alt="Moteur Spotify"> | Affichage en direct du morceau en cours avec pochette d'album en couleur, défilement artiste/titre, mini barres d'égaliseur animées et barre de progression. |
| **Google Cast & Nest**<br>`googlecast` | <img src="docs/assets/engines/engine_googlecast.png" width="240" alt="Moteur Google Cast"> | Découverte automatique mDNS des enceintes Google Home / Nest Audio avec affichage des pochettes, volume et progression de lecture en streaming. |
| **Ticker & Graphique Crypto**<br>`crypto` | <img src="docs/assets/engines/engine_crypto.png" width="240" alt="Moteur Crypto"> | Cotations en direct Binance / CoinGecko, badge de variation sur 24h et courbes sparklines historiques en temps réel avec cache TTL intelligent. |
| **Ticker Bourse & Actions**<br>`stock` | <img src="docs/assets/engines/engine_stock.png" width="240" alt="Moteur Bourse"> | Cotations en temps réel Yahoo Finance, badges de variation 1D % et graphiques d'aires sparkline intrajournaliers pour actions et ETF NASDAQ/S&P. |
| **Météo Dynamique**<br>`weather` | <img src="docs/assets/engines/engine_weather.png" width="240" alt="Moteur Météo"> | Conditions extérieures en direct, températures max/min, humidité, vent et icônes rétro animées via OpenWeatherMap & Open-Meteo. |
| **Capteur Climat Intérieur**<br>`temp` | <img src="docs/assets/engines/engine_temp.png" width="240" alt="Moteur Température"> | Température intérieure (°C/°F) et humidité relative en temps réel via le capteur I2C intégré SHTC3 avec indicateurs dynamiques de confort. |
| **Sonomètre Décibelmètre**<br>`decibel` | <img src="docs/assets/engines/engine_decibel.png" width="240" alt="Moteur Décibels"> | Surveillance calibrée du niveau sonore ambiant en dB SPL avec smileys arcade réactifs, jauge supérieure style barre de vie VS fighting et VU-mètre segmenté. |
| **HUD Télémétrie Système**<br>`sysinfo` | <img src="docs/assets/engines/engine_sysinfo.png" width="240" alt="Moteur SysInfo"> | Moniteur double colonne en temps réel de l'utilisation CPU (%), RAM (%), température matérielle du SoC (°C/°F) et Uptime avec jauges colorées. |
| **Ticker Actualités en Direct**<br>`gnews` | <img src="docs/assets/engines/engine_gnews.png" width="240" alt="Moteur GNews"> | Derniers grands titres en temps réel avec badges de catégories (`[TECH]`, `[MONDE]`, `[ÉCO]`), balise clignotante de direct et bandeau défilant fluide à 60 FPS. |
| **Home Assistant & MQTT**<br>`mqttdata` | <img src="docs/assets/engines/engine_mqttdata.png" width="240" alt="Moteur MQTT Data"> | Tableaux de bord Home Assistant, valeurs de capteurs, graphiques historiques sur 24h et météo locale diffusés via MQTT avec blueprints prêts à l'emploi. |
| **Visualiseur Audio**<br>`visualizer` | <img src="docs/assets/engines/engine_visualizer.png" width="240" alt="Moteur Visualiseur"> | Barres de spectre 32 bandes en dégradé arc-en-ciel avec points de crête (peak hold), formes d'ondes oscilloscope et modes audio-réactifs radiaux. |
| **Lecteur GIF Animés**<br>`gif` | <img src="docs/assets/engines/engine_gif.png" width="240" alt="Moteur GIF"> | Lecture fluide à 60 FPS d'animations rétro et de playlists sur carte SD avec accélération DMA canvas zéro-copie. |
| **Bandeau Défilant Marquee**<br>`message` | <img src="docs/assets/engines/engine_message.png" width="240" alt="Moteur Message"> | Enseignes matricielles personnalisables avec typographie ambrée lumineuse, multiples directions de défilement et déclencheurs API REST. |
| **Marquee Borne d'Arcade**<br>`marquee` | <img src="docs/assets/engines/engine_marquee.png" width="240" alt="Moteur Marquee"> | Affichage des marquees de jeux d'arcade officiels via intégration Pixelcade pour configurations Batocera, Recalbox et RetroPie. |
| **Calendrier & Date**<br>`date` | <img src="docs/assets/engines/engine_date.png" width="240" alt="Moteur Date"> | Grande date au style arcade avec ombres portées 3D Capcom/Nintendo, support multilingue (FR, EN, ES) et synchronisation NTP/RTC DS3231. |

> [!NOTE]
> **Avertissement sur le rendu matériel :** Les captures d'écran, visuels d'aperçu des moteurs et posters d'horloges présentés dans cette documentation sont des simulations logicielles haute fidélité destinées à illustrer la mise en page, les animations et la disposition des éléments. Le rendu visuel réel sur un panneau LED HUB75 physique peut légèrement différer selon le pas de masque (pitch), le filtre diffuseur acrylique, la luminosité des LEDs et l'éclairage ambiant.

---

## 🎮 Horloges Rétro-Gaming Légendaires (Posters & Simulations HUB75)

> [!NOTE]
> Les visuels ci-dessous sont des simulations logicielles haute fidélité. Le rendu réel sur dalle LED HUB75 physique peut présenter de légères variations d'affichage (diffusion optique, colorimétrie et luminosité perçue).

> [!IMPORTANT]
> **Disponibilité selon le profil matériel (ESP32-S3 vs ESP32 Standard) :**
> En raison de l'empreinte mémoire Flash ROM des assets graphiques haute résolution sur les cartes à 4 Mo de Flash, **Metal Slug** (Thème 41), **Pokédex** (Thème 32), **World Clock** (Thème 33) et **Words Clock** (Thème 37) sont exclusifs aux **cartes ESP32-S3 (16 Mo de Flash)**. Sur les cartes ESP32 DevKit classiques, la sélection de ces thèmes bascule automatiquement et de façon transparente vers l'horloge Arcade standard.

ArcadeMatrix intègre une collection exclusive d'horloges rétro arcade et consoles synchronisées au matériel, rendues avec des sprites d'origine 100 % fidèles au pixel près à 60 FPS constants, avec zéro allocation dynamique sur la boucle chaude Core 1 :

### 1. Metal Slug: Super Vehicle-001 (SNK Neo Geo) — Thème 41 *(Exclusif ESP32-S3)*
*Pixel art authentique SNK Neo Geo avec décor de bazar arabe désertique, soldat Marco Rossi en combat, char Rebel Di-Cokka, hélicoptère de patrouille et fusillades à la mitrailleuse lourde.*
![Horloge Metal Slug](docs/assets/clocks/poster_metal_slug.png)

### 2. Castlevania (Konami NES) — Thème 31
*Beffroi gothique avec Simon Belmont gravissant le grand escalier de pierre vers la chambre de Dracula, torche sur piédestal vacillante, chauve-souris vampire traversant la lune de sang, et chiffres gothiques ivoire haute lisibilité.*
![Horloge Castlevania](docs/assets/clocks/poster_castlevania.png)

### 3. Super Mario Bros (NES) — Thème 30
*Overworld du Royaume Champignon avec briques SMB1 authentiques ; Mario traverse l'écran et saute sous le bloc pour déclencher un rebond élastique et le basculement du chiffre (avec coup de carapace Koopa verte et jaillissement d'une pièce dorée en mode compact 128x32).*
![Horloge Super Mario Bros](docs/assets/clocks/poster_super_mario.png)

### 4. Mega Man (Capcom NES) — Thème 35
*Forteresse Wily de Capcom avec plateformes techniques séparées, jauge d'énergie vitale, Metool endormi et Mega Man tirant au Buster sur le pod des minutes.*
![Horloge Mega Man](docs/assets/clocks/poster_megaman.png)

### 5. Sonic The Hedgehog (Sega Genesis) — Thème 39
*Plateformes à damier de Green Hill Zone, anneaux dorés tournoyants, ressort rouge, badnik Motobug et animations d'attente et de saut spin-dash de Sonic.*
![Horloge Sonic The Hedgehog](docs/assets/clocks/poster_sonic.png)

### 6. Pokémon Pokédex (Nintendo Game Boy) — Thème 32 *(Exclusif ESP32-S3)*
*Interface Pokédex (Pocket Index) authentique à double écran avec hublot d'inspection, sprite animé de Pikachu, horloge numérique en police PKMN, barre de télémétrie des secondes et témoin LED clignotant.*
![Horloge Pokédex](docs/assets/clocks/poster_pokedex.png)

### 7. Pac-Man Arcade (Namco 1980) — Thème 26
*Labyrinthe arcade Namco original avec couloirs néon bleu, pastilles, Pac-Man animé et les 4 fantômes poursuivants (Blinky, Pinky, Inky, Clyde).*
![Horloge Pac-Man](docs/assets/clocks/poster_pacman.png)

### 8. Tetris Arcade (Alexey Pajitnov 1984) — Thème 23
*Casse-tête légendaire de briques tombantes avec minos 3D biseautés dynamiques (I, J, L, O, S, T, Z) descendant en cascade pour assembler les heures et les minutes en temps réel.*
![Horloge Tetris](docs/assets/clocks/poster_tetris.png)

### 9. World Clock & Terminateur Solaire — Thème 33 *(Exclusif ESP32-S3)*
*Carte du monde continentale haute résolution avec terminateur solaire jour/nuit dynamique calculant la déclinaison solaire en temps réel, méridien local et double heure UTC/locale.*
![Horloge World Map](docs/assets/clocks/poster_worldmap.png)

### 10. True Matrix Rain (Les Wachowski 1999) — Thème 21
*Pluie numérique emblématique avec cascades de glyphes vert phosphore, vitesses de chute aléatoires, têtes blanches lumineuses, traînées de rémanence et chiffres néon éclatants.*
![Horloge Matrix Rain](docs/assets/clocks/poster_matrix_rain.png)

---

## 🚀 Compatibilité Universelle des Moteurs : Auto Depth & Auto Buffer

ArcadeMatrix intègre un pipeline d'exécution ultra-optimisé avec gestion intelligente de la mémoire vive, permettant aux 18 moteurs (y compris les plus gourmands comme `AnimatedGIF`, `Stock`, `Crypto` et `MUGEN`) de tourner sans compromis sur **ESP32 classique (sans PSRAM)** comme sur **ESP32-S3 (16 Mo de PSRAM)** :

- **🎨 Auto Color Depth (`dynamic_color_depth`)** :
  - **Couleurs 8 Bits Riches par Défaut** : Horloges, combats MUGEN, visualiseurs, messages et marquees s'affichent avec la qualité maximale en 8 bits (jusqu'à 256 niveaux de luminosité par composante RVB).
  - **Récupération Dynamique de la Sandbox Mémoire** : Lorsque les moteurs réseau (`stock`, `crypto`, `weather`, `gnews`) doivent télécharger des flux HTTPS, le contrôleur HUB75 DMA bascule temporairement en 4 bits sous extinction matérielle (blanking OE). Cela libère instantanément **16 à 24 Ko de RAM DMA contiguë**, garantissant l'espace nécessaire aux certificats TLS et aux volumineux flux JSON sans fragmentation du heap.
  - **Transitions Instantanées à 0 ms** : Dès que les cotations, prix cryptos et graphiques sont valides en cache (`needsTlsFetch() == false`), la descente en 4 bits est court-circuitée. Stock et Crypto s'activent immédiatement en **qualité 8 bits intégrale avec 0 ms de latence (zéro coupure d'écran)**.

- **⚡ Pipeline de Buffering Automatique (`render_pipeline: auto`)** :
  - **Accélération PSRAM** : Sur ESP32-S3, alloue un canvas 16 bits en PSRAM avec double buffer DMA (`canvas_double`) pour une fluidité absolue à 60 FPS sans tearing.
  - **Sanctuaire DMA en SRAM1** : Sur ESP32 classique, le canvas hors-écran de 8 Ko est pré-alloué dès le boot en **SRAM1** (mémoire interne dédiée au CPU) avant le démarrage du Wi-Fi et du serveur Web. Cela préserve **8 192 octets de mémoire contiguë DMA dans la SRAM2**, évitant la saturation.
  - **Simple Buffer DMA sans Tearing** : Utilise le processeur de transfert par rafale `Hub75BulkEncoder` pour synchroniser l'écriture des trames, éliminant tout déchirement d'image même en simple buffer DMA.

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

> [!TIP]
> **Gestionnaire de Fichiers & Uploader Web (Sans retirer la carte SD !) :**
> Vous pouvez désormais téléverser, gérer, renommer et supprimer vos dossiers de playlists GIF directement depuis votre navigateur grâce à la carte **GIF File Manager** de la Web UI avec support natif Horizontal / Vertical et réindexation automatique en arrière-plan — réalisé par [@TooncesToo](https://github.com/TooncesToo) !

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

## ⚡ Compatibilité Matérielle & Moteurs

| Moteur / Fonctionnalité | Catégorie | ESP32-S3 (Carte Waveshare) | ESP32 Classique (DevKit / `esp32dev`) | Prérequis Matériel / Réseau |
| :--- | :--- | :---: | :---: | :--- |
| **Horloge & Watch Faces (`clock`)** | `info` | 🟢 60 FPS | 🟢 60 FPS | Polices bitmap dynamiques, thèmes rétro/arcade |
| **Animations GIFs (`gifs`)** | `media` | 🟢 Fullspeed 60 FPS | 🟢 Fullspeed 60 FPS | Carte Micro-SD (orientations Yoko / Tate) |
| **Combat M.U.G.E.N (`fighter`)** | `arcade` | 🟢 60 FPS | 🟢 60 FPS | Carte Micro-SD (streaming sprites RGB565) |
| **Tableau de Bord Desk Deck (`dashboard`)** | `info` | 🟢 10 FPS | 🟢 10 FPS | Wi-Fi (Horloge multi-widgets, météo & marchés) |
| **Crypto en Temps Réel (`crypto`)** | `finance` | 🟢 10 FPS (8 bits) | 🟢 10 FPS (8 bits en cache) | Wi-Fi, HTTPS/TLS (Binance, CoinGecko) |
| **Bourse & Graphiques Sparklines (`stock`)** | `finance` | 🟢 10 FPS (8 bits) | 🟢 10 FPS (8 bits en cache) | Wi-Fi, HTTPS/TLS (Yahoo Finance) |
| **Actualités en Direct (`gnews`)** | `news` | 🟢 30 FPS | 🟢 30 FPS | Wi-Fi, HTTPS/TLS (API GNews) |
| **Météo en Direct (`weather`)** | `info` | 🟢 10 FPS | 🟢 10 FPS | Wi-Fi (OpenWeatherMap, Open-Meteo) |
| **Date & Calendrier (`date`)** | `info` | 🟢 30 FPS | 🟢 30 FPS | Heure système locale & fond sprite optionnel |
| **Défilement de Texte (`message`)** | `text` | 🟢 60 FPS | 🟢 60 FPS | Scroller textuel sous-pixel à 60 FPS |
| **Télémétrie Système (`sysinfo`)** | `system` | 🟢 10 FPS | 🟢 10 FPS | Jauges temps réel CPU, RAM, Temp & Uptime |
| **Marquee Rétro Gameroom (`marquee`)** | `arcade` | 🟢 60 FPS | 🟢 60 FPS | MQTT / Batocera / Recalbox / RetroPie |
| **Spotify Now Playing (`spotify`)** | `media` | 🟢 30 FPS | 🟢 30 FPS | Wi-Fi, HTTPS/TLS, API Web Spotify |
| **Affichage Google Cast (`google_cast`)** | `media` | 🟢 30 FPS | 🟢 30 FPS | Wi-Fi, découverte locale mDNS |
| **WebRadio Autonome (`music`)** | `media` | 🟢 30 FPS (DAC I2S) | ❌ Incompatible | DAC ES8311 & PSRAM requis |
| **Spectre Audio FFT (`audiovisualizer`)** | `audio` | 🟢 60 FPS (Micro I2S) | ❌ Incompatible | Double micro ES7210 requis |
| **Sonomètre SPL / Décibels (`decibel`)** | `audio` | 🟢 30 FPS (Micro I2S) | ❌ Incompatible | Double micro ES7210 requis |
| **Capteur Climat Intérieur (`temp`)** | `sensor` | 🟢 30 FPS (I2C SHTC3) | ❌ Incompatible | Capteur température/humidité SHTC3 requis |
| **Home Assistant & Données MQTT (`mqttdata`)** | `info` | 🟢 30 FPS | 🟢 30 FPS | Broker MQTT / Blueprints Home Assistant |

👉 *Pour l'analyse technique exhaustive (budgets FPS, RAM interne et DMA), consultez la [Matrice de Compatibilité des Moteurs](docs/ENGINE_COMPATIBILITY_MATRIX.md).*

> [!NOTE]
> ### 💡 Compatibilité Totale TLS sur ESP32 Classique (`esp32dev`) & Préfetch de Transition
> L'ESP32 standard (WROOM-32 sans PSRAM) est désormais **totalement compatible** avec les moteurs connectés HTTPS/TLS (`crypto`, `stock`, `gnews`, `weather`) grâce à un **bac à sable mémoire à libération DMA** :
> 1. **Pourquoi une pause d'environ 1 seconde lors de la première rotation ?** Sur ESP32 classique, le balayage physique de l'écran HUB75 DMA et le handshake cryptographique mbedTLS ne peuvent pas coexister en même temps en raison des limites de mémoire SRAM interne contiguë (~45 Ko requis par mbedTLS). Lors du basculement vers un moteur TLS, le firmware relâche temporairement le framebuffer DMA, ouvrant une **fenêtre mémoire propre de 89 Ko** pour pré-télécharger en lot tous les cours, graphiques et icônes en keep-alive HTTP/1.1 en ~700 ms avant de reconfigurer l'écran.
> 2. **Rotations Suivantes Instantanées :** Ce préfetch de transition n'a lieu **qu'une seule fois** lors de la première rotation ! Pour toutes les rotations suivantes tant que le cache est frais (durée configurable depuis l'UI, ex: 10 à 15 minutes), le moteur affiche instantanément les cours et courbes depuis la RAM à **pleine profondeur 8 bits, sans aucun délai de transition ni trafic réseau**.
> 3. **Rafraîchissement Automatique :** À l'expiration du cache, le moteur effectue un rafraîchissement transparent lors de la rotation suivante et réinitialise le cycle.

> [!NOTE]
> **Détection Matérielle Dynamique & Dégradation Douce :** Tous les capteurs matériels (Gyroscope `QMI8658`, Microphone `ES7210`, DAC `ES8311`, Capteur de température `SHTC3`) sont sondés dynamiquement au démarrage sur le bus I2C/I2S. Si un composant est absent de votre carte, la fonctionnalité est **automatiquement désactivée sans aucun plantage**, avec repli sur le pilotage manuel via l'interface Web.

- **Carte ESP32-S3 Waveshare RGB Matrix (`esp32s3_waveshare`)** : **100% compatible avec toutes les fonctionnalités.** Fortement recommandée. Indispensable pour les grands panneaux **256x64**, le streaming audio WebRadio, l'auto-rotation gyroscopique, et exploite les capteurs matériels intégrés (Décibelmètre, Température, DAC HP) directement.
- **ESP32 Classique (WROOM-32 / `esp32dev`)** : Processeur double cœur Tensilica Xtensa LX6 @ 240MHz. Supporte pleinement les animations, l'interface Web, MUGEN et désormais tous les moteurs cloud HTTPS/TLS (`crypto`, `stock`, `gnews`, `weather`) pour les matrices **128x32 / 64x32**. Les capteurs physiques audio/température sont absents des DevKits standards sauf câblage externe.

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
- [Guide d'intégration Home Assistant](docs/HOME_ASSISTANT_FR.md)
- [Blueprints Home Assistant](tools/home_assistant/blueprints/)
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
- **[Clockwise](https://github.com/jnthas/clockwise)** par Jonathas Amaral Barbosa (@jnthas) pour les designs d'horloges pixel art et assets rétro Clockwise (Mario, Pokédex, World Map, Words).
- **[@TooncesToo](https://github.com/TooncesToo)** (Erik Jerue) pour le développement du moteur Home Assistant & Données MQTT avec blueprints prêts à l'emploi, l'API de bibliothèque GIF réseau, le téléversement multi-fichiers, le gestionnaire de fichiers Web UI avec support double orientation sur ESP32 et Raspberry Pi, les optimisations des cadrans d'horloge et ses contributions majeures au projet.

Un grand merci à la **RPiTeam** pour le super pack de 600 GIFs !

## 📜 Licence
Ce projet est publié sous la **[PolyForm Noncommercial License 1.0.0](LICENSE)**.

**En résumé :** vous êtes libre d'utiliser, modifier et partager ce projet pour tout usage non-commercial (usage personnel, projet hobbyiste, recherche, éducation, organismes publics/à but non lucratif) - voir le fichier [LICENSE](LICENSE) complet pour les termes exacts. **Tout usage commercial (vente d'unités assemblées, de kits, ou de produits/services dérivés) nécessite une licence séparée - contactez [Red1L](https://github.com/red77290) pour discuter des conditions commerciales.**
