# Exigences matérielles

🇬🇧 [English](HARDWARE.md) | 🇫🇷 Français | 🇪🇸 [Español](HARDWARE_ES.md)

## Modèles ESP32
Le firmware ArcadeMatrix prend en charge les cartes ESP32 standard, mais les exigences changent selon la taille de votre matrice :

### 128x32 ou plus petit (usage standard)
- **Carte :** ESP32 WROOM standard (par ex. NodeMCU ESP32S, ESP32 Dev Module).
- **RAM :** la SRAM standard convient parfaitement.
- **Câblage :** utilise les broches VSPI/HSPI standard. Voir [WIRING_FR.md](WIRING_FR.md) pour le brochage.

### 256x64 ou plus grand (usage avancé)
- **Carte :** **ESP32-S3 avec PSRAM** (par ex. ESP32-S3 WROOM-1 N8R8).
- **RAM :** la PSRAM est **OBLIGATOIRE** pour le double buffering en profondeur de couleur 24 bits sur de grands panneaux.
- **Pourquoi ?** Un affichage 256x64 requiert ~98KB par frame. Le double buffering demande ~200KB de RAM DMA contiguë, ce que l'ESP32 standard ne peut pas fournir de manière fiable tout en maintenant le Wi-Fi et le serveur Web. L'ESP32-S3 déporte cela de manière transparente vers la PSRAM ou dispose de suffisamment de blocs contigus pour éviter les crashs OOM (Out Of Memory).

### ESP32-S3 Waveshare (Support 100% testé et validé sur matériel réel)
La carte **Waveshare ESP32-S3 Matrix Board** (8MB Flash + PSRAM) est **100% prise en charge et validée physiquement sur du matériel réel**. 
Le profil dédié `HARDWARE_PROFILE_WAVESHARE_S3` (`pio run -e esp32s3_waveshare`) remappe les broches HUB75 sur des GPIO libres (A=18, B=8, C=3, D=42, E=9) et utilise l'interface SD_MMC 1-bit rapide (CMD=44, CLK=1, D0=17), éliminant tout conflit avec la PSRAM octal. Tout fonctionne impeccablement sans aucun conflit. Voir [WIRING_FR.md](WIRING_FR.md) pour le tableau de brochage complet.

## Panneaux multiples : chaînage vs vraies grilles/murs 2D (runtime vs compile-time)
Le build RPi (`ArcadeMatrix_RPi`) utilise la bibliothèque `rpi-rgb-led-matrix`, qui expose `--led-chain`,
`--led-parallel` et `--led-rows` comme options **entièrement configurables à l'exécution** — un Raspberry Pi dispose de 2 à 3 connecteurs GPIO HUB75 indépendants, donc construire un mur 2D de panneaux (par ex. 2 rangées x 2 colonnes) n'est qu'un changement de config, sans recompilation.

Les cartes ESP32 n'exposent qu'une **seule** sortie HUB75. Ce firmware prend déjà en charge `CHAIN=N` dans
`conf.ini` (`ConfigLoader::matrix.chainLength`) pour chaîner des panneaux **sur une seule ligne** à l'exécution
(par ex. `CHAIN=4` pour un ruban 512x32) — cela fonctionne aujourd'hui et ne nécessite aucun changement de firmware.

**Les vraies grilles/murs 2D (plusieurs rangées de panneaux chaînés, par ex. un mur 2x2) ne sont PAS actuellement intégrés à ce firmware.** La bibliothèque sous-jacente `ESP32-HUB75-MatrixPanel-I2S-DMA` fournit bien un helper
`VirtualMatrixPanel_T` qui remappe des coordonnées virtuelles (x,y) sur un chaînage serpentin / zig-zag de panneaux pour construire un tel mur, mais c'est une **classe template C++** — sa forme de chaînage et son type de scan sont des paramètres **au moment de la compilation**, pas quelque chose qui peut être lu depuis `conf.ini` au boot comme tous les autres réglages de ce projet. L'intégrer proprement nécessiterait soit :
1. Un flag / environnement PlatformIO dédié par agencement de mur (recompile + reflash pour changer le layout), ou
2. Refactorer tous les moteurs (~46 call sites) du type concret `MatrixPanel_I2S_DMA*` vers une interface commune basée sur `Adafruit_GFX*`, afin de pouvoir substituer une instance `VirtualMatrixPanel_T`.

Les deux options sont loin d'être triviales et constituent un vrai manque architectural par rapport au support runtime complet de `--led-parallel` sur RPi — c'est suivi comme limitation connue au lieu d'être ignoré silencieusement. Le chaînage sur une seule rangée via `CHAIN=` reste aujourd'hui la manière prise en charge pour construire un affichage plus grand.

### Utilisation de la flash
Le firmware n'utilise jamais SPIFFS / LittleFS — tous les assets runtime (GIF, sprites de combattants, playlists,
`conf.ini`) vivent sur la carte SD externe. Pour cette raison, l'environnement PlatformIO `esp32dev` utilise
`board_build.partitions = min_spiffs.csv` au lieu de la table de partitions par défaut d'Arduino-ESP32 : cela
conserve la même disposition OTA double banque (deux slots d'app, donc `/api/update` continue de fonctionner) mais
agrandit chaque slot d'app de 1.25MB à ~1.875MB en récupérant la partition SPIFFS d'environ 900KB, sinon gaspillée. Au moment de la rédaction, le firmware `esp32dev` utilise ~66 % de son slot d'app (contre 98 %+ avant ce changement) — une marge confortable pour les fonctionnalités PNG / icônes météo prévues ensuite.

## Matériel de matrice
- **Type :** panneaux de matrice LED RGB HUB75 / HUB75E (P2, P2.5, P3, P4, P5).
- **Puces driver :** compatibles avec les registres à décalage standard (FM6126A, ICN2038S, etc.).
- **Alimentation :** une alimentation 5V dédiée est requise. Une matrice 64x32 peut tirer jusqu'à 4 A en blanc plein. **N'alimentez jamais la matrice directement depuis l'ESP32 !**
