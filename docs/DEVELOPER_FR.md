# Guide développeur (ESP32)

🇬🇧 [English](DEVELOPER.md) | 🇫🇷 Français | 🇪🇸 [Español](DEVELOPER_ES.md)

Bienvenue dans le guide de développement ESP32 d'ArcadeMatrix. Ce document explique comment étendre le projet C++, en particulier comment ajouter une nouvelle horloge et comment l'exposer à l'API Web.

---

## 1. Ajouter une nouvelle horloge spécialisée

Grâce à l'architecture modulaire couplée au matériel d'ArcadeMatrix, ajouter une nouvelle horloge consiste à dériver ou instancier une classe d'affichage sous `src/engines/clocks/` qui gère sa propre logique et son rendu direct.

### Matrice de Compatibilité des Thèmes d'Horloge (IDs 0 à 29)

| ID | Thème / Horloge | Classe / Fichier | 128x32 | 256x64 | Police Perso (.amf) | Couleurs Persos | Statut Matériel |
|---|---|---|:---:|:---:|:---:|:---:|:---:|
| 0 | Nintendo | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 1 | Capcom | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 2 | Taito | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 3 | Sega | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 4 | Cave | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 5 | Konami | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 6 | SNK | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 7 | Technos | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 8 | IGS | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 9 | Hudson | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 10 | Banpresto | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 11 | Namco | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 12 | Ryu | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 13 | Mario | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 14 | Marco | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 15 | Megaman | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 16 | Bub | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 17 | Space Invaders | `ArcadeClock` | ✓ | ✓ | — | — | Validé matériel |
| 18 | Cyberpunk | `CyberpunkClock` | ✓ | ✓ | — | — | Validé matériel |
| 19 | FlipClock | `FlipClock` | ✓ | ✓ | — | — | Validé matériel |
| **20** | **Custom Gradient** | `ClockEngine` | **✓** | **✓** | **✓** | **✓** | **Validé matériel** |
| 21 | PongClock | `PongClock` | ✓ | ✓ | — | — | Validé matériel |
| 22 | TetrisClock | `TetrisClock` | ✓ | ✓ | — | — | Validé matériel |
| 23 | TetrisGameboy | `TetrisGameboyClock` | ✓ | ✓ | — | — | Validé matériel |
| 24 | PacmanClock | `PacmanClock` | ✓ | ✓ | — | — | Validé matériel |
| 25 | WordClock | `WordClock` | ✓ | ✓ | — | — | Validé matériel |
| 26 | BinaryClock | `BinaryClock` | ✓ | ✓ | — | — | Validé matériel |
| 27 | VersusClock | `VersusClock` | ✓ | ✓ | — | — | Validé matériel |
| 28 | SlotMachineClock | `SlotMachineClock` | ✓ | ✓ | — | — | Validé matériel |
| 29 | MatrixRainClock | `MatrixRainClock` | ✓ | ✓ | — | — | Validé matériel |

---

### Étape par étape

1. **Créer le header (`src/engines/clocks/MyClock.h`) :**
   ```cpp
   #pragma once
   #include <Arduino.h>
   #include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
   #include "core/ConfigLoader.h"

   class MyClock {
   public:
       MyClock(MatrixPanel_I2S_DMA* display, ConfigLoader* config);
       void loop(); // Appelé à chaque frame
   private:
       MatrixPanel_I2S_DMA* _display;
       ConfigLoader* _config;
   };
   ```

2. **Créer l'implémentation (`src/engines/clocks/MyClock.cpp`) :**
   ```cpp
   #include "MyClock.h"

   MyClock::MyClock(MatrixPanel_I2S_DMA* display, ConfigLoader* config) : _display(display), _config(config) {}

   void MyClock::loop() {
       if (!_display) return;
       _display->fillScreen(0);
       _display->drawPixel(10, 10, _display->color565(255, 0, 0));
   }
   ```

3. **Enregistrer l'horloge dans `ClockEngine.cpp` :**
   - Incluez votre header dans `src/engines/ClockEngine.cpp`.
   - Instanciez votre horloge selon le thème `clock_theme` sélectionné par l'utilisateur.
   - Déclenchez l'appel dans `ClockEngine::loop()`.

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

---

## 5. Limites Connues & Sécurité (Known Limitations & Security)

### Sécurité
- **API non authentifiée** : L'API REST HTTP et le serveur WebUI s'exécutent sans authentification sur le réseau local. ArcadeMatrix est conçu pour être utilisé sur un réseau de confiance (LAN privé). Ne pas exposer directement le port 80 à Internet sans reverse proxy authentifié.
- **CORS** : Les en-têtes `Access-Control-Allow-Origin: *` sont activés par défaut pour permettre le contrôle depuis des applications Web locales.

### Limites Connues
- **Pas de rollback OTA automatique** : Le processus de mise à jour OTA écrit dans la partition secondaire et bascule le bootloader. En cas de démarrage avec un firmware fonctionnellement défectueux, il n'y a pas de mécanisme de restauration automatique sans re-flashage physique ou WebSerial.
- **Réseau Synchrone dans les Providers** : Bien que le serveur HTTP `AsyncWebServer` soit asynchrone, les rafraîchissements réseau des providers (`CryptoEngine`, `StockEngine`, `WeatherEngine`) s'exécutent de manière synchrone avec mise en cache locale. En cas de perte de connexion, le dernier état en cache est conservé sans bloquer la boucle d'affichage principale.
- **Carte SD requise pour GIF et MUGEN** : Les animations GIF et les sprites MUGEN nécessitent impérativement une carte SD installée et formatée en FAT32 ou exFAT.

---

## 6. Tutoriel : Ajouter un nouveau module de rotation (ex: Pager/News)

Si vous souhaitez ajouter un nouveau module d'affichage (ex: "Pager" pour afficher des actualités) à la boucle de rotation, suivez ces étapes depuis l'UI jusqu'au backend.

### Étape 1 : L'interface Web
Les boutons de rotation sont définis dans `api/www/index.html`. Ajoutez votre nouveau module sous forme de case à cocher :
```html
<label class="toggle-checkbox">
  <input type="checkbox" value="pager">
  <span class="toggle-label">Pager</span>
</label>
```
Le JS intégré (`app.js`) analyse automatiquement toutes les cases cochées et les envoie sous forme de chaîne de caractères (ex: `rotation: "clock,gifs,pager"`) au point d'accès `/api/settings`.

### Étape 2 : La configuration (`conf.ini`)
Dans `src/core/ConfigLoader.cpp`, la chaîne est lue et sauvegardée sur la carte SD :
```cpp
// Dans ConfigLoader::parseConfig() sous "[IDLE]"
if (key == "ROTATION") idle.rotation = value;

// Et pour la sauvegarder dans ConfigLoader::saveConfig()
out += "ROTATION=" + idle.rotation + "\n";
```
*Note : Comme elle est stockée en tant que chaîne, `idle.rotation` stockera nativement `"clock,gifs,pager"` sans nécessiter de modification du parseur.*

### Étape 3 : Le moteur d'affichage (Engine)
Créez un nouveau moteur (ex: `src/engines/PagerEngine.h`) implémentant les méthodes de base :
```cpp
#pragma once
#include "core/Matrix.h"

class PagerEngine {
public:
    void init() {
        // Configurer le client HTTP pour récupérer les actus
    }
    
    void draw() {
        matrix->clearScreen();
        // Dessiner l'UI avec matrix->setCursor() et matrix->print()
    }
    
    void stop() {
        // Libérer la mémoire
    }
};
```

### Étape 4 : La boucle de rotation
Dans `src/core/RotationManager.cpp` (ou `main.cpp`), la chaîne de rotation est découpée. Quand c'est au tour du pager de jouer :
```cpp
if (currentModule == "pager") {
    pagerEngine->init();
    // Dans la boucle main loop()
    pagerEngine->draw();
}
```
