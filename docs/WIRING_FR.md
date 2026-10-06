# Guide de câblage & Pièges d'intégration matérielle

🇬🇧 [English](WIRING.md) | 🇫🇷 Français | 🇪🇸 [Español](WIRING_ES.md)

Câbler une matrice LED HUB75 et des périphériques de stockage à un ESP32 exige une rigueur absolue. Comme le HUB75 utilise des transferts DMA continus et que l'ESP32 multiplexe ses GPIO à travers une matrice de broches logicielle (GPIO Matrix), une mauvaise assignation de broches ou un conflit de bibliothèques peut provoquer de graves collisions de bus, des échecs de montage de la carte SD ou des boucles de redémarrage par watchdog.

ArcadeMatrix prend officiellement en charge deux profils matériels distincts :
1. **ESP32 Standard (`esp32dev`)** — ESP32-WROOM / ESP32-D0WD classique (Matériel DMDos Board V3 / RetroPixelLED).
2. **Waveshare ESP32-S3 RGB Matrix (`esp32s3_waveshare`)** — ESP32-S3 avec 16 Mo de PSRAM Octale + 32 Mo de Flash.

---

## 1. ESP32 Standard (`esp32dev` / DMDos Board V3)

Ce profil est conçu pour les cartes ESP32 classiques dotées de 320 Ko de SRAM interne, utilisant le DMA HUB75 standard et une carte MicroSD connectée sur le bus matériel VSPI.

### 1.1 Brochage de la matrice LED HUB75

| Broche HUB75 | GPIO ESP32 | Description | Remarques matérielles |
| :--- | :--- | :--- | :--- |
| **R1** | 25 | Rouge haut | Ligne de données |
| **G1** | 26 | Vert haut | Ligne de données |
| **B1** | 27 | Bleu haut | Ligne de données |
| **R2** | 14 | Rouge bas | Ligne de données |
| **G2** | 12 | Vert bas | ⚠️ *Broche de strapping (MTDI). Ne doit pas être tirée à HIGH au boot sur cartes flash 3.3V.* |
| **B2** | 13 | Bleu bas | Ligne de données |
| **A** | 33 | Adresse de ligne A | Ligne d'adresse |
| **B** | 32 | Adresse de ligne B | Ligne d'adresse |
| **C** | 22 | Adresse de ligne C | Ligne d'adresse |
| **D** | 17 | Adresse de ligne D | Ligne d'adresse |
| **E** | **GND / -1** | Adresse de ligne E | **Panneaux 32px (scan 1/16) : Relier à GND.** Panneaux 64px : GPIO 21. |
| **LAT (STB)**| 4 | Latch | Verrou d'horloge |
| **OE** | 15 | Output Enable | Blanking PWM actif à l'état bas. ⚠️ *Broche de strapping (MTDO).* |
| **CLK** | 16 | Horloge Matrice | Horloge DMA parallèle |

> [!CAUTION]
> **PIÈGE CRITIQUE : NE JAMAIS ASSIGNER LA BROCHE E DU HUB75 AU GPIO 18 !**
> Le GPIO 18 est strictement réservé à l'horloge SPI de la MicroSD (`VSPI_SCK`).
> Sur les panneaux 64x32 standards (scan 1/16), la broche `E` n'est **pas utilisée** et doit être reliée à la **masse (GND)** côté panneau (le logiciel passe `-1`).
> Assigner la broche E au GPIO 18 détourne l'horloge SPI et rend la carte MicroSD totalement inutilisable !

### 1.2 Brochage de la carte MicroSD (Bus VSPI)

Le lecteur MicroSD utilise le bus matériel VSPI dédié :

| Broche SD | GPIO ESP32 | Signal VSPI | Remarques de câblage |
| :--- | :--- | :--- | :--- |
| **CS** | 5 | Chip Select | Géré par GPIO logiciel (actif à l'état BAS). Doit être à HIGH au repos/boot. |
| **CLK / SCK**| 18 | Horloge SPI | Horloge SPI jusqu'à 25 MHz. **Ne JAMAIS partager avec HUB75.** |
| **MISO / DO**| 19 | Données sortantes (Carte -> ESP32) | Nécessite un pull-up (`INPUT_PULLUP`). |
| **MOSI / DI**| 23 | Données entrantes (ESP32 -> Carte) | Sortie maître SPI. |
| **VCC** | 3.3V / 5V | Alimentation | Connecter à un 3.3V propre (ou 5V si le module a son régulateur LDO 3.3V). |
| **GND** | GND | Masse commune | Doit impérativement être reliée à la masse de l'ESP32. |

### 1.3 Interfaces de contrôle & Périphériques

| Composant | GPIO ESP32 | Description |
| :--- | :--- | :--- |
| **Bouton (PIN)** | 21 | Bouton poussoir multifonction (Clic court : navigation / Clic long : sélection). Tiré à la masse. |
| **Récepteur IR** | 34 | Démodulateur infrarouge protocole NEC (GPIO d'entrée seule). |
| **I2C SDA** | 21 | Bus capteur optionnel (partagé avec le bouton si I2C actif). |
| **I2C SCL** | 22 | Bus capteur optionnel (en conflit avec la ligne C de la matrice sur câblage classique). |

---

## 2. Pièges techniques & Règles d'architecture pour la MicroSD ESP32

Si vous modifiez l'initialisation du HAL ou le code de stockage, respectez rigoureusement ces 6 invariants anti-régression :

### 🔴 Piège 1 : Détournement matériel du CS via `SPI.begin()`
Dans le framework Arduino-ESP32 (`esp32-hal-spi.c`), passer `SD_CS_PIN` (5) comme 4ème argument de `SPI.begin(sck, miso, mosi, ss)` appelle automatiquement `pinMatrixOutAttach(ss, SPI_SS_IDX, ...)`. Cela verrouille le GPIO 5 sur la ligne CS matérielle du contrôleur SPI.
**Or**, la bibliothèque `greiman/SdFat` pilote le CS de manière logicielle via `digitalWrite(m_csPin, level)`. Une fois mappé à `SPI_SS_IDX`, les écritures GPIO standard **ne peuvent plus forcer le niveau BAS**. La carte SD n'est alors jamais sélectionnée, provoquant une erreur `code=0x1 (CMD0 timeout), data=0xFF`.
- **Règle :** Toujours initialiser SPI avec `SPI.begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI, -1)` et détacher explicitement la broche CS : `pinMatrixOutDetach(SD_CS_PIN, false, false)`.

### 🔴 Piège 2 : Absence de `USER_SPI_BEGIN` dans `SdFat`
Dans `SdFat` (`SdSpiArduinoDriver.h`), si `spiConfig.options` ne contient pas le drapeau `USER_SPI_BEGIN`, le pilote appelle en interne `m_spi->begin()` sans argument, ce qui réinitialise tout le périphérique SPI avec les broches par défaut du framework.
- **Règle :** Passer systématiquement `SdSpiConfig(SD_CS_PIN, SHARED_SPI | USER_SPI_BEGIN, SD_SCK_MHZ(f), &SPI)`.

### 🟠 Piège 3 : Ligne MISO flottante
Beaucoup de modules passifs MicroSD bon marché n'ont pas de résistance de tirage (pull-up) sur la ligne DAT0/MISO. Durant l'initialisation et les périodes de haute impédance, un MISO flottant lit en permanence `0xFF`.
- **Règle :** Activer explicitement le pull-up interne de l'ESP32 : `pinMode(VSPI_MISO, INPUT_PULLUP)`.

### 🟠 Piège 4 : Séquence de transition en mode SPI (74 cycles d'horloge)
À la mise sous tension, toutes les cartes SD démarrent en mode natif SD Bus. Pour commuter leur automate interne en mode SPI, l'hôte doit envoyer au moins 74 cycles d'horloge (20 octets dummy de `0xFF` à 1 MHz) avec `CS = HIGH` avant de baisser CS et d'émettre la commande `CMD0`.
- **Règle :** Envoyer 20 octets dummy via `SPI.transfer(0xFF)` avec CS maintenu à HIGH avant d'appeler `sd.begin()`.

### 🔴 Piège 5 : Expiration du Watchdog pendant l'escalade multi-fréquence (TG1WDT ~9.2s)
Le watchdog matériel du bootloader ESP32 (TG1WDT) a un délai d'environ 9,2 secondes.
Lorsque `sd.begin()` tente une cascade de 4 fréquences (25 -> 16 -> 10 -> 4 MHz), chaque tentative infructueuse attend jusqu'à 2000 ms (`SD_INIT_TIMEOUT`). Si 4 tentatives échouent consécutivement, 8 secondes s'écoulent. Avec la surcharge du boot, le temps total atteint ~9,2s et déclenche un `TG1WDT_SYS_RESET`, provoquant une boucle de redémarrage perpétuelle.
- **Règle :** Toujours rafraîchir le watchdog (`esp_task_wdt_reset()`) entre chaque essai de fréquence et limiter l'échelle de fréquences.

### 🔴 Piège 6 : Formatage monolithique de la partition NVS
Appeler `nvs_flash_erase()` de manière synchrone sur une partition NVS corrompue désactive le cache CPU et fige les interruptions FreeRTOS. Sur les partitions de taille conséquente, cette opération dépasse le seuil de l'Interrupt Watchdog Timer (IWDT), causant un crash `TG1WDT_SYS_RESET`.
- **Règle :** Formater la partition NVS secteur par secteur par blocs de 4 Ko avec `esp_task_wdt_reset()` et `delay(5)` entre chaque secteur.

---

## 3. Carte Waveshare ESP32-S3 Matrix (`esp32s3_waveshare`)

La carte Waveshare ESP32-S3 (N32R16 : 32 Mo Flash + 16 Mo PSRAM Octale) intègre un routage d'usine dédié et utilise le bus natif haute vitesse 1-bit `SD_MMC`.

### 3.1 Brochage officiel Waveshare S3

| Signal HUB75 | GPIO ESP32-S3 | Signal SD_MMC | GPIO ESP32-S3 | Périphériques intégrés | GPIO ESP32-S3 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **R1** | 4 | **D0 (DAT0)** | 17 | **I2C SDA** | 47 (SHTC3 @ 0x70, ES7210 @ 0x40) |
| **G1** | 5 | **CMD** | 44 | **I2C SCL** | 48 |
| **B1** | 6 | **CLK** | 1 | **I2S MCLK** | 12 |
| **R2** | 7 | | | **I2S SCLK** | 43 |
| **G2** | 15 | | | **I2S LRCK** | 38 |
| **B2** | 16 | | | **I2S ASDOUT** | 39 |
| **A** | 18 | | | **Sortie DAC (ES8311)**| 21 |
| **B** | 8 | | | | |
| **C** | 3 | | | | |
| **D** | 42 | | | | |
| **E** | 9 | | | | |
| **LAT** | 40 | | | | |
| **OE** | 2 | | | | |
| **CLK** | 41 | | | | |

### 3.2 Pièges techniques & Règles d'architecture pour la Waveshare S3

### 🔴 Piège 1 : Zone d'exclusion stricte PSRAM Octale OPI (GPIO 33 à 37)
Les 16 Mo de PSRAM Octale requièrent 8 lignes de données et une horloge différentielle. Sur l'ESP32-S3 N32R16, **les GPIO 33, 34, 35, 36 et 37 sont définitivement mobilisés par l'interface interne de la PSRAM**.
- **Règle :** Ne JAMAIS assigner ni manipuler les GPIO 33 à 37 dans le logiciel. Tout accès ou attachement de périphérique sur ces broches provoque un kernel panic instantané et irrécupérable.

### 🔴 Piège 2 : SD_MMC natif vs SPI
Le slot MicroSD de la carte Waveshare S3 est directement relié au contrôleur matériel SDMMC de l'ESP32-S3 (GPIO 1, 44, 17). Il ne supporte **PAS** le protocole SPI.
- **Règle :** Toujours compiler avec `USE_SD_MMC = 1` et initialiser via `SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN)`. Ne jamais inclure ni instancier `SdFat` sur la cible S3.

### 🟠 Piège 3 : Contexte VFS Fat en DRAM interne (`max_files=3`)
Lors de l'appel à `SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 3)`, le paramètre `max_files=3` est crucial. Il garantit que le descripteur de système de fichiers FatFs (~1,7 Ko) est alloué en SRAM interne rapide plutôt que d'être relégué en PSRAM externe. Cela évite les blocages d'éviction de lignes de cache face au moteur GDMA du HUB75 qui lit les framebuffers en PSRAM.

---

## 4. Recommandations d'Alimentation (Toutes cartes)

- **Alimentation 5V dédiée :** Une matrice LED RGB 64x32 peut consommer jusqu'à 4,0 A à pleine intensité blanche.
- **Masse commune :** Toujours relier la masse (GND) de l'alimentation 5V externe directement à la broche `GND` de l'ESP32.
- **Ne jamais alimenter les panneaux par le port USB :** Les ports USB fournissent généralement entre 500 mA et 1 A, ce qui provoque des chutes de tension (brownout) sur l'ESP32, entraînant des écritures corrompues sur la carte SD ou des crashs watchdog.
