# Prérequis Matériels & Matrice de Capacités

[English](HARDWARE.md) | 🇫🇷 Français | 🇪🇸 [Español](HARDWARE_ES.md)

## 1. Profils de Cartes Supportés

ArcadeMatrix prend en charge trois cibles firmware réparties sur deux paliers de capacités matérielles :

### 1.1 ESP32 Standard (`esp32dev`)
- **Microcontrôleur** : ESP32 double cœur classique (NodeMCU, ESP32 Dev Module).
- **RAM** : ~320 Ko de SRAM interne (sans PSRAM).
- **Stockage** : Carte SD en SPI (VSPI : `CS=5, SCK=18, MISO=19, MOSI=23`).
- **Résolution Matrice** : Jusqu'à 128x32 ou 64x64.
- **Audio / Capteur** : Modules optionnels externes INMP441 / SHTC3.

### 1.2 ESP32-S3 Générique N16R8 / N8R8 (`esp32s3_n16r8`)
- **Microcontrôleur** : ESP32-S3 double cœur avec **Flash Quad SPI** (`8 Mo` ou `16 Mo`, `QIO`) + **PSRAM Octale** (`8 Mo`, `OPI`) — ex. `ESP32-S3-DevKitC-1-N16R8` ou `N8R8`.
- **Détection Mémoire Dynamique** : Détecte automatiquement la taille réelle de la Flash (`8 Mo` / `16 Mo` / `32 Mo`) et de la PSRAM au démarrage.
- **Stockage** : `SD_MMC` 1-bit rapide (`CLK=1, CMD=44, D0=17` ; tirer `DAT3/CS` au `3.3V` via une résistance de `10kΩ`).
- **Résolution Matrice** : Jusqu'à 256x64 (double tampon DMA SPIRAM supporté).

### 1.3 Waveshare ESP32-S3 Matrix Board (`esp32s3_waveshare`)
- **Microcontrôleur** : ESP32-S3 double cœur (`N32R16V`) avec **32 Mo Flash Octale (`OPI`)** + **16 Mo PSRAM Octale (`OPI`)**.
- **Stockage** : `SD_MMC` 1-bit rapide (`D0=17, CMD=44, CLK=1`).
- **Périphériques intégrés** : Codec audio I2S ES7210 + microphone numérique, DAC ES8311, capteur température/humidité SHTC3.
- **Résolution Matrice** : Jusqu'à 256x64 (double tampon DMA SPIRAM supporté).

### 1.4 Brochage ESP32-S3 (`esp32s3_n16r8` & `esp32s3_waveshare`)

| Sous-système | Signal | GPIO ESP32-S3 | Notes |
|---|---|---|---|
| **Matrice HUB75** | `R1, G1, B1` | `GPIO 4, 5, 6` | Données RGB moitié supérieure |
| **Matrice HUB75** | `R2, G2, B2` | `GPIO 7, 15, 16` | Données RGB moitié inférieure |
| **Matrice HUB75** | `A, B, C, D, E` | `GPIO 18, 8, 3, 42, 9` | Adressage lignes (`E` requis pour panneaux 64 lignes) |
| **Matrice HUB75** | `LAT, OE, CLK` | `GPIO 40, 2, 41` | Latch, Output Enable, Horloge pixel |
| **MicroSD (SD_MMC 1-bit)** | `CLK` | `GPIO 1` | Horloge matérielle SDMMC |
| **MicroSD (SD_MMC 1-bit)** | `CMD` | `GPIO 44` | Commande / Réponse (pull-up `10kΩ` vers `3.3V`) |
| **MicroSD (SD_MMC 1-bit)** | `DAT0 (D0)` | `GPIO 17` | Ligne de données 0 (pull-up `10kΩ` vers `3.3V`) |
| **MicroSD (SD_MMC 1-bit)** | `DAT3 / CS` | `3.3V` | Tirer au `3.3V` via résistance `10kΩ` (maintient le mode SD natif) |
| **Bus I2C** | `SDA, SCL` | `GPIO 47, 48` | Capteurs optionnels `SHTC3` (`0x70`), `QMI8658` (`0x6B`), `MPU6050` (`0x68`) |
| **Audio I2S** | `MCLK, BCLK, WS, DIN, DOUT` | `GPIO 12, 43, 38, 39, 21` | ES7210/ES8311 intégrés ou Micro/DAC I2S externe |

---

## 2. Matrice de Disponibilité des Moteurs

| Moteur / Fonction | ID Canonique | ESP32 Standard (Sans PSRAM) | Waveshare ESP32-S3 (16Mo PSRAM) | Prérequis (`EngineRequirements`) |
|---|---|---|---|---|
| **Horloge** (36 Thèmes) | `clock` | ✅ Actif | ✅ Actif | Aucun |
| **Date** | `date` | ✅ Actif | ✅ Actif | Aucun |
| **Météo** | `weather` | ✅ Actif | ✅ Actif | `needsNetwork=true` |
| **Lecteur GIF** | `gifs` | ✅ Actif (Streaming SD) | ✅ Actif (Buffer PSRAM) | `needsSd=true` |
| **Ticker Crypto** | `crypto` | ✅ Actif (Profondeur 4-bit dynamique + préchargement) | ✅ Actif (8-bit + Cache) | `needsNetwork=true`, `needsTls=true` |
| **Ticker Bourse** | `stock` | ✅ Actif (Profondeur 4-bit dynamique + préchargement) | ✅ Actif (8-bit + Cache) | `needsNetwork=true`, `needsTls=true` |
| **Visualiseur Audio** | `audiovisualizer` | ⚠️ Actif si micro I2S détecté | ✅ Actif (ES7210 intégré) | `needsAudio=true` |
| **Sonomètre Décibel** | `decibelMeter` | ⚠️ Actif si micro I2S détecté | ✅ Actif (ES7210 intégré) | `needsAudio=true` |
| **Température & Humidité** | `temp` | ⚠️ Actif si SHTC3 détecté | ✅ Actif (SHTC3 intégré) | `needsTempSensor=true` |
| **Message Personnalisé** | `message` | ✅ Actif | ✅ Actif | Aucun |
| **Google Cast / Nest** | `google_cast` | ✅ Actif | ✅ Actif | `needsNetwork=true` |
| **Lecteur Spotify** | `spotify` | ✅ Actif (Profondeur 4-bit dynamique) | ✅ Actif | `needsNetwork=true`, `needsTls=true` |
| **Moniteur Système** | `system_info` | ✅ Actif | ✅ Actif | Aucun |
| **Lecteur Musical Universel** | `music_player` | 🚫 Désactivé (*Requiert PSRAM & Sortie Audio*) | ✅ Actif | `needsPsram=true`, `needsAudioOutput=true` |
| **Dashboard Multi-Widgets** | `dashboard` | ✅ Actif (Profondeur 4-bit dynamique + préchargement) | ✅ Actif | Aucun (Réseau/Capteur optionnels) |
| **Ticker GNews Live** | `gnews` | ✅ Actif (Profondeur 4-bit dynamique + cache SD) | ✅ Actif | `needsNetwork=true`, `needsTls=true` |
| **Marquee Arcade** | `marquee` | ✅ Actif | ✅ Actif | Aucun |
| **Données MQTT** | `mqttdata` | ✅ Actif (Jusqu'à 4 pages) | ✅ Actif (Jusqu'à 6 pages) | `needsNetwork=true` |
| **Overlay Fighter** *(Transversal)* | `fighter` *(overlay)* | ✅ Actif (Mode 32px) | ✅ Actif (Mode 64px) | `needsSd=true` |

---

## 3. Alimentation Électrique
- **Matrice LED RGB** : Panneaux HUB75 / HUB75E (P2, P2.5, P3, P4, P5).
- **Alimentation 5V dédiée** : Une matrice 64x32 peut consommer jusqu'à 4A à pleine luminosité blanche. **Alimentez toujours la matrice directement depuis l'alimentation 5V, et JAMAIS via la broche 5V de l'ESP32.**
