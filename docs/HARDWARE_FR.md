# Prérequis Matériels & Matrice de Capacités

[English](HARDWARE.md) | 🇫🇷 Français | 🇪🇸 [Español](HARDWARE_ES.md)

## 1. Profils de Cartes Supportés

ArcadeMatrix prend en charge deux profils matériels physiques :

### 1.1 ESP32 Standard (`esp32dev`)
- **Microcontrôleur** : ESP32 double cœur classique (NodeMCU, ESP32 Dev Module).
- **RAM** : ~320 Ko de SRAM interne (sans PSRAM).
- **Stockage** : Carte SD en SPI (VSPI).
- **Résolution Matrice** : Jusqu'à 128x32 ou 64x64.
- **Audio / Capteur** : Modules optionnels externes INMP441 / SHTC3.

### 1.2 Waveshare ESP32-S3 Matrix Board (`esp32s3_waveshare`)
- **Microcontrôleur** : ESP32-S3 double cœur avec 32 Mo Flash + 16 Mo de PSRAM Octal.
- **Stockage** : SD_MMC 1-bit rapide (D0=17, CMD=44, CLK=1).
- **Périphériques intégrés** : Codec audio I2S ES7210 + microphone numérique, capteur température/humidité SHTC3.
- **Résolution Matrice** : Jusqu'à 256x64 (double tampon DMA SPIRAM supporté).

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
