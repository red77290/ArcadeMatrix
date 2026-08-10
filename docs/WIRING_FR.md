# Guide de câblage

🇬🇧 [English](WIRING.md) | 🇫🇷 Français | 🇪🇸 [Español](WIRING_ES.md)

Câbler une matrice LED HUB75 à un ESP32 exige des connexions précises. Les broches de données de la matrice sont mappées vers les GPIO de l'ESP32.

## Brochage ESP32 standard (WROOM)
Voici le mapping de câblage par défaut pour le moteur DMA.

| Broche HUB75 | GPIO ESP32 | Description |
|--------------|------------|-------------|
| R1        | 25         | Rouge haut |
| G1        | 26         | Vert haut |
| B1        | 27         | Bleu haut |
| R2        | 14         | Rouge bas |
| G2        | 12         | Vert bas |
| B2        | 13         | Bleu bas |
| A         | 33         | Adresse A |
| B         | 32         | Adresse B |
| C         | 22         | Adresse C |
| D         | 17         | Adresse D |
| E         | 18         | Adresse E (pour les panneaux de 64px de haut) |
| LAT (STB) | 4          | Latch |
| OE        | 15         | Output Enable |
| CLK       | 16         | Clock |

*Remarque : la broche E n'est requise que si votre matrice fait 64 pixels de haut (par ex. taux de scan 1/32). Sur les panneaux de 32px de haut (scan 1/16), la broche `E` du panneau est en général non connectée ou reliée à la masse (GND) côté panneau, donc GPIO18 reste exclusivement dédié à l'horloge SPI de la carte SD ci-dessous - pas de conflit dans cette configuration (très courante).*

## Câblage de la carte SD (les deux cartes)

La carte SD utilise un bus SPI distinct (`SPI.begin()` dans `src/main.cpp`), indépendant des
broches I2S/DMA dédiées de la matrice HUB75 ci-dessus.

| Broche SD | GPIO ESP32 | Description |
|-----------|------------|-------------|
| CS        | 5          | Chip Select |
| SCK       | 18         | Horloge SPI |
| MISO      | 19         | Données depuis la carte |
| MOSI      | 23         | Données vers la carte |

ℹ️ GPIO18 est partagé sur le papier entre cette ligne SCK de la SD et la broche d'adresse HUB75
`E` définie ci-dessus, mais ce n'est **pas un conflit** dans le cas courant : sur les panneaux de
32px de haut (scan 1/16), `E` n'est pas utilisée/câblée vers l'ESP32 (souvent reliée à la masse
directement sur le panneau), donc GPIO18 est libre pour la carte SD - c'est la configuration
testée/fonctionnelle. Ce n'est que si vous câblez un véritable panneau de 64px de haut où `E` est
réellement connectée à GPIO18 que les deux partageraient la même broche physique ; dans ce cas
précis, remappez soit la ligne SCK de la SD (`VSPI_SCK` dans `src/main.cpp`) soit la broche `E`
du HUB75 (`_pins` dans `src/core/MatrixEngine.cpp`) vers un GPIO libre avant de câbler les deux.
Si votre carte SD ne se monte pas (`sdWait Failed` / `sdSelectCard Failed` dans le log série),
les causes les plus probables sont : la carte n'est pas formatée en FAT32, elle n'est pas bien
insérée, ou votre alimentation ne peut pas fournir matrice + SD + Wi-Fi simultanément (le
régulateur intégré d'une carte ESP32 nue est souvent insuffisant pour un panneau entièrement
câblé - utilisez une alimentation 5V/3A+ dédiée alimentant à la fois le panneau et l'ESP32).

## Câblage ESP32-S3 Waveshare (100% Testé & Validé sur Matériel Réel)

La carte **Waveshare ESP32-S3 Matrix Board** (8MB Flash + PSRAM) intègre son propre câblage HUB75 et son bus SD_MMC 1-bit. Le profil dédié `HARDWARE_PROFILE_WAVESHARE_S3` dans `include/HardwareProfile.h` (`pio run -e esp32s3_waveshare`) remappe automatiquement toutes les broches pour **contourner complètement la plage GPIO 33-37 réservée à la PSRAM Octal**.

**Cette configuration est 100% testée et validée physiquement sur du matériel réel avec un fonctionnement fluide.**

### Brochage officiel Waveshare ESP32-S3 (HUB75 & SD_MMC)

| Signaux HUB75 | GPIO ESP32-S3 | Signaux Carte SD (SD_MMC) | GPIO ESP32-S3 |
| :--- | :--- | :--- | :--- |
| **R1** | 4 | **D0** | 17 |
| **G1** | 5 | **CMD** | 44 |
| **B1** | 6 | **CLK** | 1 |
| **R2** | 7 | | |
| **G2** | 15 | | |
| **B2** | 16 | | |
| **A** | 18 | | |
| **B** | 8 | | |
| **C** | 3 | | |
| **D** | 42 | | |
| **E** | 9 | | |
| **LAT** | 40 | | |
| **OE** | 2 | | |
| **CLK** | 41 | | |

*Toutes les broches HUB75 et SD sont situées en dehors de la plage critique GPIO 33-37 réservée par la PSRAM octal, ce qui garantit un fonctionnement parfait sur dalles 256x64 et 128x32 sans aucun conflit.*
