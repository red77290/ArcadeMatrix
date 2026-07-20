# Premiers pas (firmware ESP32, première installation de PlatformIO)

🇬🇧 [English](GETTING_STARTED.md) | 🇫🇷 Français | 🇪🇸 [Español](GETTING_STARTED_ES.md)

Ce guide s'adresse aux développeurs qui n'ont jamais utilisé [PlatformIO](https://platformio.org/) auparavant et qui souhaitent compiler, flasher et déboguer le firmware ArcadeMatrix en local. Pour le câblage matériel, voir `docs/HARDWARE_FR.md`/`docs/WIRING_FR.md` ; pour les options de `conf.ini`, voir `docs/CONFIGURATION_FR.md` ; pour l'architecture du codebase, voir `docs/ARCHITECTURE_FR.md` ; pour les workflows de contribution (ajout d'horloges, endpoints REST, polices personnalisées), voir `docs/DEVELOPER_FR.md`.

## 1. Installer PlatformIO

Vous n'avez besoin que d'**une** de ces options - choisissez celle qui correspond à votre workflow :

- **Extension VS Code (recommandée pour les débutants) :** installez
  [Visual Studio Code](https://code.visualstudio.com/), puis l'
  [extension PlatformIO IDE](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)
  depuis la marketplace Extensions. Elle embarque son propre Python / toolchain - aucune installation séparée n'est nécessaire.
- **CLI uniquement** (fonctionne dans n'importe quel terminal, indépendamment de l'éditeur) :
  ```bash
  pip install -U platformio
  # or, on macOS with Homebrew:
  brew install platformio
  ```
  Vérifiez que cela fonctionne : `pio --version`.

Toutes les commandes ci-dessous utilisent la CLI `pio` - si vous utilisez l'extension VS Code, les mêmes actions sont disponibles depuis la barre latérale PlatformIO (icônes build/upload/monitor) et produisent des résultats identiques ; la CLI est montrée ici car elle se copie/colle facilement et fonctionne de la même manière sur n'importe quel OS / éditeur.

## 2. Ouvrir le workspace

```bash
git clone <this-repo-url>
cd ArcadeMatrix
```

Aucun `pio project init` n'est nécessaire - `platformio.ini` existe déjà à la racine du dépôt et définit les deux cartes prises en charge comme environnements de build séparés : `esp32dev` (ESP32 classique, 4MB flash) et `esp32s3` (ESP32-S3, 8MB flash + PSRAM optionnelle). Voir `docs/HARDWARE_FR.md` pour savoir lequel correspond à votre carte et connaître ses limites spécifiques de GPIO / résolution.

## 3. Compiler le firmware

```bash
# Build both environments (fastest way to sanity-check your changes):
pio run -e esp32dev -e esp32s3

# Or just the one you actually own:
pio run -e esp32dev
```

Le premier build télécharge la toolchain Espressif ainsi que toutes les bibliothèques listées dans `lib_deps` de `platformio.ini` (pilote HUB75 DMA, AnimatedGIF, PNGdec, ESPAsyncWebServer, ArduinoJson, etc.) - cela peut prendre quelques minutes la première fois, puis c'est mis en cache dans `~/.platformio/`. Un build réussi affiche un résumé d'utilisation `RAM:` / `Flash:` et se termine par `[SUCCESS]`.

## 4. Le flasher sur votre carte

Branchez l'ESP32 / ESP32-S3 en USB, puis :

```bash
pio run -e esp32dev -t upload      # replace esp32dev with esp32s3 if that's your board
```

PlatformIO détecte automatiquement le port série dans la plupart des cas. S'il choisit le mauvais (par ex. si vous avez plusieurs périphériques USB-série connectés), indiquez-le explicitement :

```bash
pio device list                     # find the right port name
pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0   # Linux/macOS example
pio run -e esp32dev -t upload --upload-port COM5           # Windows example
```

<a id="flashing-a-pre-built-release"></a>
### Flash d'une release précompilée

Si vous ne voulez pas compiler depuis les sources, téléchargez `ArcadeMatrix-esp32dev.zip` ou
`ArcadeMatrix-esp32s3.zip` depuis la [dernière release](https://github.com/red77290/ArcadeMatrix/releases/latest)
à la place - chacun contient `firmware-*.bin`, `bootloader-*.bin`, `partitions-*.bin` et
`boot_app0.bin`, compilés et validés par la CI. Flashez les quatre avec `esptool.py` aux offsets utilisés
par le partitionnement par défaut d'Arduino-ESP32 (les mêmes offsets que ceux utilisés par le Web Installer dans le navigateur -
voir `webinstaller/README_FR.md`) :

```bash
pip install esptool
esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash   0x1000  bootloader-esp32dev.bin   0x8000  partitions-esp32dev.bin   0xE000  boot_app0.bin   0x10000 firmware-esp32dev.bin
```

Pour `esp32s3`, utilisez `--chip esp32s3` et l'offset `0x0` pour le bootloader au lieu de `0x1000` (l'en-tête du bootloader ROM du S3 diffère) - voir le tableau des offsets de flash dans `webinstaller/README_FR.md` pour le détail complet.

## 5. Lire les logs série

Le firmware journalise la progression du boot, l'état du Wi-Fi, le résultat du montage de la carte SD, l'utilisation du heap ainsi que les erreurs / avertissements d'exécution sur le port série à `115200` bauds (voir `monitor_speed` dans `platformio.ini`) :

```bash
pio device monitor -e esp32dev -b 115200
```

Appuyez sur `Ctrl+C` pour quitter. Pour redémarrer la carte et voir les logs de démarrage, appuyez sur `Ctrl+T` suivi de `Ctrl+R` dans le moniteur. Combinez build + flash + monitor en une seule commande pour une boucle de dev rapide :

```bash
pio run -e esp32dev -t upload && pio device monitor -e esp32dev -b 115200
```

## 6. Préparer la carte SD

Le firmware nécessite une carte SD externe (câblée selon `docs/WIRING_FR.md`, chip-select sur GPIO 5 par
défaut - voir `SD_CS_PIN` dans `src/main.cpp`) pour :
- `/conf.ini` — vos paramètres Wi-Fi / matrice / thèmes (généré automatiquement avec des valeurs par défaut au premier démarrage s'il est absent ; modifiez-le directement sur la carte, ou via `/api/settings` dans l'interface Web une fois le Wi-Fi actif).
- `/gifs/`, playlists d'assets `.gif` / `.raw` / `.png` (voir §4 de `docs/ARCHITECTURE_FR.md` pour les différences de format entre les trois).
- `/fighters_32/` ou `/fighters_64/` — feuilles de sprites `.fgt` dérivées de MUGEN (voir `tools/mugen_extractor/README_FR.md` pour générer les vôtres à partir de fichiers de personnages MUGEN).
- Facultatif : `/fonts/*.amf` — polices bitmap personnalisées chargeables depuis la SD (voir la section « Charger une police bitmap personnalisée depuis la SD » de `docs/DEVELOPER_FR.md` et `tools/bdf_to_amfont/`).

Une carte formatée en FAT32 est requise (standard pour les cartes jusqu'à 32GB ; les cartes plus grandes peuvent devoir être reformatées de exFAT vers FAT32).

## 7. Exécuter la suite de tests

```bash
pio test -e esp32dev
```

**Point important :** `test/test_config/test_config.cpp` est un test Unity **sur cible** - il compile contre le vrai cœur Arduino ESP32 (`WiFi.h`, `FS.h`, etc.) et doit être **uploadé sur une carte physique** pour être exécuté (PlatformIO le flashe, puis lit les résultats pass/fail sur le port série). Il n'existe actuellement aucune cible de test indépendante du matériel (« native » / hôte) pour ce firmware - voir `docs/ARCHITECTURE_FR.md` et `docs/DEVELOPER_FR.md` pour comprendre pourquoi (le codebase s'appuie largement sur des API spécifiques ESP32 comme `SD.h` / `WiFi.h`, qui n'ont pas d'équivalents desktop directement interchangeables sans un effort de mocking plus important). C'est aussi pour cela que la CI (`.github/workflows/build.yml`) se contente de **compiler** la cible de test (`pio test -e <env> --without-uploading --without-testing`) au lieu de l'exécuter : les runners GitHub Actions n'ont pas d'ESP32 physique branché, mais une passe de compilation seule détecte quand même les régressions de build (includes obsolètes, signatures cassées, etc.) à chaque push / PR. Si vous avez une carte connectée en local, la commande `pio test -e esp32dev` (sans flags) est la bonne pour vraiment la flasher et l'exécuter.

## Dépannage

- **`pio: command not found`** après `pip install` : le répertoire des scripts Python n'est pas sur votre `PATH`. Utilisez plutôt l'extension VS Code, ou ajoutez à votre profil shell le chemin `bin` affiché par `pip show -f platformio`.
- **L'upload échoue / time out** : maintenez le bouton `BOOT` / `IO0` de la carte pendant le démarrage de l'upload (certaines cartes de développement ESP32 en ont besoin pour entrer dans le bootloader), ou baissez `upload_speed` dans `platformio.ini`.
- **Le build échoue avec une erreur de bibliothèque manquante** : supprimez `.pio/` puis relancez le build - un cache de bibliothèques corrompu est la cause la plus fréquente (`rm -rf .pio && pio run -e esp32dev`).
- **`sdWait Failed` / `sdSelectCard Failed` / `Check status failed` quelques fois juste au démarrage, puis le firmware continue normalement (Wi-Fi se connecte, l'heure NTP s'affiche correctement)** : c'est bénin - ce sont de simples tentatives internes du driver SD de l'ESP32 pendant son handshake d'initialisation (fréquent avec certaines cartes/marques SD à la vitesse de sondage par défaut), pas un vrai échec de montage. Si la SD échouait réellement, `setup()` afficherait `CRITICAL ERROR: SD Card Mount Failed!` et resterait bloqué indéfiniment (redémarrage via le watchdog toutes les ~30s) - il n'atteindrait jamais l'étape de connexion Wi-Fi. N'investiguez le câblage/l'alimentation que si vous voyez cette erreur critique précise, ou si les lectures/écritures SD continuent d'échouer bien après le démarrage (pas juste au tout début). Les erreurs `does not exist, no permits for creation` juste après sont normales/sans gravité au premier démarrage (ex. `playlists_selected.json`, `fighters_32/index.txt` n'existent tout simplement pas tant que vous n'avez pas sauvegardé une playlist / lancé `mugen_extractor`). Consultez le tableau « Câblage de la carte SD » de `docs/WIRING_FR.md` uniquement si vous câblez une carte neuve depuis zéro.
- **`AsyncTCP.cpp: begin(): failed to start task`** juste après la connexion Wi-Fi : FreeRTOS n'arrive pas à allouer une tâche pour la pile TCP asynchrone, presque toujours à cause d'un heap interne libre trop bas (les gros buffers DMA du HUB75 sur un ESP32 sans PSRAM peuvent en consommer l'essentiel). Vérifiez `ESP.getFreeHeap()` (affiché après l'init de la matrice) - s'il ne reste que quelques Ko, réduisez `mxconfig.min_refresh_rate`/la résolution du panneau, ou passez à un ESP32-S3 avec PSRAM pour les gros panneaux. Le serveur web peut démarrer partiellement malgré cet avertissement, mais attendez-vous à ce qu'il soit instable tant que le heap libre n'est pas résolu.
