# Guide développeur (ESP32)

🇬🇧 [English](DEVELOPER.md) | 🇫🇷 Français | 🇪🇸 [Español](DEVELOPER_ES.md)

Bienvenue dans le guide de développement ESP32 d'ArcadeMatrix. Ce document explique comment étendre le projet C++, en particulier comment ajouter une nouvelle horloge et comment l'exposer à l'API Web.

---

## 1. Ajouter une nouvelle horloge spécialisée

En raison de l'architecture monolithique de la version ESP32, ajouter une horloge signifie créer une classe C++ qui gère sa propre logique et son propre dessin matériel.

### Étape par étape

1. **Créer le header (`include/MyClock.h`) :**
   ```cpp
   #pragma once
   #include <Arduino.h>
   #include "MatrixDisplay.h" // Your HUB75 matrix wrapper
   #include "ConfigLoader.h"

   class MyClock {
   public:
       MyClock(MatrixDisplay* display, Config* config);
       void loop(); // Called every frame
   private:
       MatrixDisplay* _display;
       Config* _config;
       
       // Your state variables
       int _snakeLength;
   };
   ```

2. **Créer l'implémentation (`src/MyClock.cpp`) :**
   ```cpp
   #include "MyClock.h"

   MyClock::MyClock(MatrixDisplay* display, Config* config) {
       _display = display;
       _config = config;
       _snakeLength = 3;
   }

   void MyClock::loop() {
       // 1. Clear screen or draw background
       _display->fillScreen(0);

       // 2. Execute logic
       // ...

       // 3. Draw directly using Adafruit GFX primitives
       _display->drawPixel(10, 10, _display->color565(255, 0, 0));
   }
   ```

3. **Enregistrer l'horloge dans `ClockEngine.cpp` :**
   - Incluez votre header en haut de `src/ClockEngine.cpp`.
   - Instanciez votre horloge dynamiquement (ou comme variable membre) dans le `ClockEngine` selon le thème sélectionné par l'utilisateur.
   - Exemple dans `ClockEngine::loop()` :
     ```cpp
     switch (_config->time_theme) {
         case 22:
             if (!_myClock) _myClock = new MyClock(_display, _config);
             _myClock->loop();
             break;
         // ...
     }
     ```

---

## 2. Modifier l'API Web et la configuration

Si votre nouvelle horloge a besoin de nouveaux paramètres utilisateur (par ex. `snake_speed`), vous devez modifier toute la chaîne de configuration, du frontend jusqu'à la struct.

1. **Mettre à jour la struct (`src/core/ConfigLoader.h`) :**
   Ajoutez votre nouvelle variable à la struct principale `Config`.
   ```cpp
   struct Config {
       // ... existing fields
       int snake_speed = 5;
   };
   ```

2. **Mettre à jour le parseur et le générateur JSON (`src/api/WebServerAPI.cpp`) :**
   L'API communique en JSON (`ArduinoJson`).
   - Trouvez la méthode qui sérialise la configuration pour l'envoyer au navigateur et ajoutez :
     `doc["snake_speed"] = config.snake_speed;`
   - Trouvez la méthode qui analyse le JSON entrant depuis le navigateur et ajoutez :
     `if (doc.containsKey("snake_speed")) config.snake_speed = doc["snake_speed"].as<int>();`

3. **Mettre à jour le chargeur du système de fichiers (`src/core/ConfigLoader.cpp`) :**
   Assurez-vous que votre nouvelle variable est lue depuis le `conf.ini` de la carte SD et sauvegardée dedans afin qu'elle survive aux redémarrages.

4. **Mettre à jour l'interface Web (`src/api/WebUI.h`, généré depuis le frontend Vue - voir `scripts/build_webui.py`) :**
   - Ajoutez les champs HTML correspondant à votre paramètre.
   - Mettez à jour le JS frontend pour envoyer votre nouvelle variable dans le payload JSON quand l'utilisateur clique sur « Save ».

### Endpoints REST notables (liste non exhaustive)

| Endpoint | Méthode | Rôle |
|----------|---------|------|
| `/api/status` | GET | Uptime, heap libre / min libre, stats PSRAM. |
| `/api/settings` | GET/POST | Lecture/écriture complète de la config (persiste dans `conf.ini`). |
| `/api/wifi` | POST | `{ssid, password}` - enregistre les identifiants et tente une reconnexion immédiate, en renvoyant le succès/l'échec de manière synchrone (ne nécessite pas de redémarrage). |
| `/api/marquee` | POST | Corps d'image RGB565 brut (little-endian, row-major, exactement `width*height*2` octets correspondant à la résolution configurée du panneau - voir `tools/mugen_extractor` pour la même convention de format sur le fil). L'affiche immédiatement pendant ~8 s, en interrompant la rotation au repos, puis la reprend. Il n'y a pas de décodeur d'image embarqué, donc toute intégration bridge / frontend doit préconvertir l'artwork (PNG/JPEG/box-art) vers ce format brut avant le POST. |
| `/api/update` | POST | Upload OTA du firmware (`Update.h`), écrit dans le slot de partition OTA inactif. |

### Intégration marquee/box-art de type Pixelcade (frontends de bornes d'arcade)

Contrairement à `ArcadeMatrix_RPi` (qui télécharge les artworks marquee Pixelcade à la demande depuis GitHub,
voir son `core/dmd_cache.py`), l'ESP32 n'a ni budget flash/RAM/CPU disponible pour un client HTTPS
récupérant des images en plein jeu, ni cache disque extensible sans limite au fil du temps. L'architecture
recommandée consiste à la place à pré-mettre en cache tous les artworks **sur la carte SD à l'avance**, afin
qu'à l'exécution l'affichage ne soit qu'une simple recherche rapide de fichier local - aucun réseau n'est
impliqué quand un jeu démarre :

1. **Configuration unique :** lancez `tools/pixelcade_sync/pixelcade_sync.sh` (macOS/Linux) ou
   `pixelcade_sync.ps1` (Windows) sur votre PC (pas sur l'ESP32) pour
   télécharger le dépôt d'artworks Pixelcade et l'organiser sous `/pixelcade/<system>/<game>.png`.
   Copiez ensuite le résultat sur votre carte SD. Consultez le README de cet outil pour filtrer
   uniquement les systèmes que vous utilisez (le dépôt complet pèse plusieurs centaines de Mo).
2. **Configuration unique :** lancez `tools/recalbox_daemon/install.sh` (macOS/Linux) ou `install.ps1`
   (Windows) depuis votre PC pour installer via SSH un petit daemon d'événements sur votre appareil
   Recalbox/Batocera - il demande les deux IP, aucune session SSH manuelle n'est nécessaire. Il s'agit du
   *même* protocole de daemon qu'utilise `ArcadeMatrix_RPi` (`core/ssh_installer.py`), donc une seule
   installation sert les deux projets.
3. À l'exécution, le daemon publie `{"status": "playing"|"browsing"|"stopped", "game": "<rom
   basename>", "system": "<SystemId>"}` via MQTT sur `recalbox/system/playing` (ou
   `batocera/system/playing`) chaque fois que le jeu sélectionné / en cours change.
4. `RetroFrontendListener::handleGameEvent()` (firmware) l'analyse, mappe le `SystemId` vers un nom de
   dossier Pixelcade (`mapSystemToPixelcadeFolder()` - maintenu synchronisé avec le `SYSTEM_MAP` de
   `dmd_cache.py` côté RPi), puis vérifie `/pixelcade/<folder>/<game>.png` sur la carte SD :
   - Si trouvé : l'affiche immédiatement via `gif->playGif()` (décodeur PNG de GifEngine, ajouté
     en même temps que le support GIF - voir `esp32-gif-png` dans le changelog).
   - Sinon (pas encore synchronisé, ou Pixelcade n'a pas d'art pour ce jeu) : revient à un défilement
     du nom du jeu en texte via `MessageEngine`, en reproduisant le comportement du RPi quand son cache rate.
   - Sur `"status": "stopped"` : appelle `gif->stop()`, ce qui reprend la rotation GIF/horloge au repos.

Cela retire complètement du chemin d'exécution toute récupération / décodage / mise à l'échelle d'image
(fait une seule fois, hors ligne, sur un PC avec une vraie bande passante et sans contrainte mémoire),
en suivant la même philosophie « préconvertir hors ligne » que pour les assets `.raw` de GifEngine et `tools/mugen_extractor`.

**Chemins hérités / alternatifs toujours pris en charge :**
- `/api/marquee` (POST, corps RGB565 brut) reste disponible pour les scripts bridge qui veulent pousser
  directement une image arbitraire non Pixelcade (par ex. un marquee généré sur mesure) au lieu de
  s'appuyer sur la recherche Pixelcade mise en cache sur la SD décrite ci-dessus.
- Le topic natif `/Recalbox/EmulationStation/Event` (`rungame`/`stop`) reste aussi souscrit comme
  fallback de base pour les installations qui ne veulent pas installer le daemon personnalisé, même s'il
  ne transporte aucun détail sur le jeu / système (Recalbox ne l'inclut pas sur ce topic), donc seul un
  placeholder générique peut être affiché.
- L'ancien protocole bridge en texte brut `STOP_GAME`/`START_GAME:<path>` fonctionne toujours lui aussi.

---

### Charger une police bitmap personnalisée depuis la SD (BitmapFontLoader)

Par défaut, toutes les polices sont compilées dans le firmware (`src/engines/fonts/`). Pour ajouter une
police personnalisée sans recompiler le firmware :

1. Obtenez ou créez une police bitmap BDF. N'importe laquelle des polices livrées avec le projet RPi
   (`ArcadeMatrix_RPi/fonts/*.bdf`) constitue un bon point de départ, ou récupérez-en une dans une archive de polices BDF X11/X.
2. Convertissez-la au format `.amf` d'ArcadeMatrix :
   ```bash
   python3 tools/bdf_to_amfont/bdf_to_amfont.py myfont.bdf myfont.amf
   ```
   Par défaut, cela couvre l'ASCII imprimable (`0x20`-`0x7E`) ; passez `--first`/`--last` pour couvrir
   une autre plage de codepoints si la police BDF en propose une (par ex. Latin-1 étendu).
3. Copiez `myfont.amf` sur la carte SD, par ex. `/fonts/myfont.amf`.
4. Définissez `custom_font_path=/fonts/myfont.amf` sous `[fonts]` dans `conf.ini` (ou via `/api/settings`).
5. Redémarrez. `BitmapFontLoader::loadFromSD()` analyse le fichier dans une structure compatible
   `GFXfont` allouée sur le tas au démarrage, puis la fournit à `MessageEngine` pour la bannière `/api/message`
   (la police 5x7 par défaut est utilisée silencieusement en fallback si le fichier est manquant / corrompu).

**Notes de format :** `.amf` reflète exactement la structure des polices compilées d'Adafruit
(offsets de bitmap alignés sur des octets par glyphe, bits empaquetés MSB-first) — voir la docstring de
`tools/bdf_to_amfont/bdf_to_amfont.py` pour le layout binaire exact, et le parseur correspondant côté ESP32
dans `BitmapFontLoader.cpp`. Les polices sont limitées à 65535 octets de données bitmap de glyphes
empaquetées (`GFXglyph.bitmapOffset` est un `uint16_t`) — c'est la même limite que celle imposée par l'outil
`fontconvert` d'Adafruit sur les polices compilées, pas une nouvelle limitation.

---

## 3. Règles importantes pour le développement ESP32

- **Évitez les objets `String` :** utilisez des tableaux de `char` (`char[]`) autant que possible pour éviter la fragmentation du tas, fatale sur ESP32.
- **Bornes DMA :** ne dessinez jamais en dehors des limites `matrix_width` et `matrix_height`. Adafruit GFX gère la plupart des découpes, mais les écritures mémoire directes provoqueront des kernel panics.
- **Fuites mémoire :** si vous allouez dynamiquement des classes (`new MyClock()`), assurez-vous de les `delete` quand le thème change pour éviter l'épuisement mémoire.
---

## 4. Ajouter une nouvelle cible matérielle (Hardware Profile)

Si vous souhaitez porter ArcadeMatrix sur une nouvelle carte ESP32 (avec un brochage différent ou un autre type de mémoire flash/PSRAM), vous devez créer un nouveau profil matériel. Le projet utilise un système d'injection statique via des flags de compilation pour abstraire la couche matérielle.

### Étape par étape

1. **Définir le profil dans `include/HardwareProfile.h` :**
   Ajoutez un nouveau bloc `#elif defined(HARDWARE_PROFILE_MON_NOUVEL_ESP)` pour définir les broches de votre matrice HUB75 et de votre carte SD.
   ```cpp
   #elif defined(HARDWARE_PROFILE_MON_NOUVEL_ESP)
       // Profil : Mon Nouvel ESP32 Super Cool
       #define MATRIX_R1_PIN 10
       #define MATRIX_G1_PIN 11
       // ... définissez toutes les broches de la matrice ...
       
       // SD Card
       #define USE_SD_MMC 1 // 1 pour SD_MMC (rapide), 0 pour SdFat via SPI
       #define SD_MMC_D0_PIN 12
       #define SD_MMC_CMD_PIN 13
       #define SD_MMC_CLK_PIN 14
   ```

2. **Créer l'environnement dans `platformio.ini` :**
   Dans le fichier à la racine du projet, ajoutez un nouvel environnement (ex: `[env:mon_nouvel_esp]`).
   Configurez les `build_flags` pour injecter la définition créée à l'étape précédente.
   ```ini
   [env:mon_nouvel_esp]
   board = esp32-s3-devkitc-1
   build_flags = 
       -D HARDWARE_PROFILE_MON_NOUVEL_ESP
       -D CORE_DEBUG_LEVEL=0
   ```
   *Note : Le code C++ (comme `SDUtils.h`) compilera automatiquement l'interface appropriée sans aucun "if/else" dans le code métier grâce à cette injection.*

3. **Mettre à jour la CI/CD (GitHub Actions) :**
   Pour que GitHub compile automatiquement le firmware de cette nouvelle carte à chaque push ou release :
   - Ouvrez `.github/workflows/build.yml` et `.github/workflows/release.yml`.
   - Ajoutez le nom de votre environnement dans la matrice de build :
     `env: [esp32dev, esp32s3_waveshare, mon_nouvel_esp]`
   - Dans le job `Assemble site` (de `build.yml`) et `Zip per-board flashing bundles` (de `release.yml`), ajoutez les commandes de copie/zip pour votre nouvel environnement (calquées sur les autres).

4. **Créer le Manifest pour l'installateur Web (`webinstaller/`) :**
   - Créez un nouveau fichier `webinstaller/manifest-mon_nouvel_esp.json` en dupliquant un existant.
   - Mettez à jour les chemins pour qu'ils pointent vers les bons binaires (ex: `bootloader-mon_nouvel_esp.bin`).
   - Éditez `webinstaller/index.html` pour ajouter un nouveau bouton `<esp-web-install-button manifest="manifest-mon_nouvel_esp.json">` dans l'interface de flashage web.

Une fois cela fait, un simple `pio run -e mon_nouvel_esp` compilera le code entier, isolé et sécurisé, spécifiquement pour votre carte !

## Tests Unitaires & TDD
Le projet suit les principes TDD pour l'intégration des API. Lors de l'ajout d'une nouvelle API, implémentez l'interface Provider correspondante (`ICryptoProvider` etc.) et écrivez les tests unitaires avec des objets Mock avant l'intégration finale. Les tests doivent garantir une couverture maximale sur l'analyse JSON et la logique de fallback (secours) sans nécessiter de matériel physique.
