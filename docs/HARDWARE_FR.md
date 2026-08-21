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
La carte **Waveshare ESP32-S3 Matrix Board** (32MB Flash + 16MB PSRAM (N32R16)) est **100% prise en charge et validée physiquement sur du matériel réel**. 
Le profil dédié `HARDWARE_PROFILE_WAVESHARE_S3` (`pio run -e esp32s3_waveshare`) remappe les broches HUB75 sur des GPIO libres (A=18, B=8, C=3, D=42, E=9) et utilise l'interface SD_MMC 1-bit rapide (CMD=44, CLK=1, D0=17), éliminant tout conflit avec la PSRAM octal. Tout fonctionne impeccablement sans aucun conflit. Voir [WIRING_FR.md](WIRING_FR.md) pour le tableau de brochage complet.

---

## 🎙️ Capteurs Matériels & Visualiseurs Audio

### 🌡️ Capteur de Température & Humidité Intérieure (SHTC3)
- **Bus :** I2C (`SDA=47`, `SCL=48`, adresse `0x70`).
- **Fonctionnalités :** Mesure automatique de la température ambiante (°C / °F) et de l'humidité relative avec dégradé de couleur dynamique et icônes Pixel Art.
- **API Domotique :** Expose une route REST JSON `GET /api/sensor` idéale pour remonter la température intérieure dans **Home Assistant** ou un système domotique.

### 🔊 Sonomètre Décibelomètre & Visualiseur Rythmique (ES7210 / I2S DMA)
- **Bus :** Codec Audio I2C `0x40` + Bus I2S DMA (`SCLK=13`, `LRCK=12`, `ASDOUT=11`, `MCLK=14`).
- **Usage en Salle d'Arcade / Gaming Room :)** :
  - Le sonomètre est **particulièrement utile et ludique dans une salle d'arcade bruyante**, une *gaming room* ou un événement retro gaming ! Il permet de surveiller le volume sonore ambiant en temps réel, d'avertir visuellement avec des **smileys Pixel Art réactifs** (<45dB 😊 à >88dB 🚨) quand la salle devient trop bruyante, et d'ambiancer la pièce grâce aux **4 modes du visualiseur de musique rythmique** (Equalizer Spectrum, Oscilloscope Waveform, Radial Circles, Neon Fire).
- **Optimisation Lazy Sampling** : L'échantillonnage I2S DMA est actif uniquement lorsque le module est affiché à l'écran (`onActivate()`) pour économiser l'énergie et la charge CPU.

---

## Panneaux multiples : chaînage vs vraies grilles/murs 2D (runtime vs compile-time)
Le build RPi (`ArcadeMatrix_RPi`) utilise la bibliothèque `rpi-rgb-led-matrix`, qui expose `--led-chain`,
`--led-parallel` et `--led-rows` comme options **entièrement configurables à l'exécution** — un Raspberry Pi dispose de 2 à 3 connecteurs GPIO HUB75 indépendants, donc construire un mur 2D de panneaux (par ex. 2 rangées x 2 colonnes) n'est qu'un changement de config, sans recompilation.

Les cartes ESP32 n'exposent qu'une **seule** sortie HUB75. Ce firmware prend déjà en charge `CHAIN=N` dans
`config.json` (`ConfigLoader::matrix.chainLength`) pour chaîner des panneaux **sur une seule ligne** à l'exécution
(par ex. `CHAIN=4` pour un ruban 512x32) — cela fonctionne aujourd'hui et ne nécessite aucun changement de firmware.

**Les vraies grilles/murs 2D (plusieurs rangées de panneaux chaînés, par ex. un mur 2x2) ne sont PAS actuellement intégrés à ce firmware.** La bibliothèque sous-jacente `ESP32-HUB75-MatrixPanel-I2S-DMA` fournit bien un helper
`VirtualMatrixPanel_T` qui remappe des coordonnées virtuelles (x,y) sur un chaînage serpentin / zig-zag de panneaux pour construire un tel mur, mais c'est une **classe template C++** — sa forme de chaînage et son type de scan sont des paramètres **au moment de la compilation**, pas quelque شيء qui peut être lu depuis `config.json` au boot comme tous les autres réglages de ce projet.

---

## Matériel de matrice
- **Type :** panneaux de matrice LED RGB HUB75 / HUB75E (P2, P2.5, P3, P4, P5).
- **Puces driver :** compatibles avec les registres à décalage standard (FM6126A, ICN2038S, etc.).
- **Alimentation :** une alimentation 5V dédiée est requise. Une matrice 64x32 peut tirer jusqu'à 4 A en blanc plein. **N'alimentez jamais la matrice directement depuis l'ESP32 !**
