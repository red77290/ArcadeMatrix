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

| Moteur / Fonction | ESP32 Standard (Sans PSRAM) | Waveshare ESP32-S3 (16Mo PSRAM) | Prérequis (`EngineRequirements`) |
|---|---|---|---|
| **Horloge** (30 Thèmes) | ✅ Actif | ✅ Actif | Aucun |
| **Date** | ✅ Actif | ✅ Actif | Aucun |
| **Météo** | ✅ Actif | ✅ Actif | `needsNetwork=true` |
| **Lecteur GIF** | ✅ Actif (Streaming SD) | ✅ Actif (Buffer PSRAM) | `needsSd=true` |
| **Overlay Fighter** | ✅ Actif (Mode 32px) | ✅ Actif (Mode 64px) | `needsSd=true` |
| **Température & Humidité** | ⚠️ Actif si SHTC3 détecté | ✅ Actif (SHTC3 intégré) | `needsTempSensor=true` |
| **Visualizer & Sonomètre** | ⚠️ Actif si micro I2S détecté| ✅ Actif (ES7210 intégré) | `needsAudio=true` |
| **Ticker Crypto** | 🚫 Désactivé (*Requiert PSRAM*) | ✅ Actif (Cache + Cours) | `needsPsram=true`, `needsNetwork=true` |
| **Ticker Bourse** | 🚫 Désactivé (*Requiert PSRAM*) | ✅ Actif (Cache + Cours) | `needsPsram=true`, `needsNetwork=true` |
| **Message Personnalisé** | ✅ Actif | ✅ Actif | Aucun |
| **Marquee Alert** | ✅ Actif | ✅ Actif | Aucun |

---

## 3. Alimentation Électrique
- **Matrice LED RGB** : Panneaux HUB75 / HUB75E (P2, P2.5, P3, P4, P5).
- **Alimentation 5V dédiée** : Une matrice 64x32 peut consommer jusqu'à 4A à pleine luminosité blanche. **Alimentez toujours la matrice directement depuis l'alimentation 5V, et JAMAIS via la broche 5V de l'ESP32.**
