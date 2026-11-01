🇬🇧 [English](CONFIGURATION.md) | 🇫🇷 Français | 🇪🇸 [Español](CONFIGURATION_ES.md)

# Configuration Détaillée (config.json) — ESP32

Le système de configuration repose exclusivement sur un fichier unique `config.json` situé sur le stockage LittleFS / SD. Il pilote le driver DMA de la matrice, le réseau, MQTT, le système, l'orientation de l'affichage, la sécurité API, et les instances indépendantes de chaque moteur.

> `config.json` est l'**unique source de vérité**. Au démarrage, le fichier est validé et auto-réparé par `ConfigSanitizer`, garantissant que toute clé manquante est automatiquement recréée avec ses valeurs par défaut.

---

## 1. Structure Globale

```json
{
  "matrix": { ... },
  "wifi": { ... },
  "mqtt": { ... },
  "data_mqtt": { ... },
  "system": {
    "api_auth_enabled": false,
    "api_token": "",
    ...
  },
  "display": { ... },
  "audio": { ... },
  "instances": [ ... ],
  "rotation": [ ... ]
}
```

---

## 2. Le Bloc `"matrix"` (Driver Matériel)

Ce bloc configure les paramètres DMA pour la bibliothèque `ESP32-HUB75-MatrixPanel-I2S-DMA`. La modification des paramètres matériels critiques recharge automatiquement le driver.

| Clé | Type | Description |
| :--- | :--- | :--- |
| `width` | `int` | Largeur d'un seul panneau (ex. `64` ou `256`). |
| `height` | `int` | Hauteur d'un seul panneau (ex. `32` ou `64`). |
| `chain_length` | `int` | Nombre de panneaux chaînés horizontalement. |
| `driver_chip` | `String` | Puce contrôleur (`SHIFTREG`, `FM6126A`, `ICN2038S`, `MBI5124`, `SM16208`). |
| `rgb_sequence` | `String` | Ordre des couleurs (`RGB`, `RBG`, `BGR`, ...). Corrigez ici les couleurs inversées. |
| `color_depth` | `int` | Profondeur des couleurs (`0` = Auto, `1`–`8` bits sur toutes les cartes, y compris ESP32 Standard). Défaut `0` (Auto). En mode Auto ou quand le pipeline dynamique est actif, s'adapte mathématiquement à la géométrie et aux besoins du prochain moteur. Dans l'UI web, ce champ est grisé/désactivé quand le Pipeline de Présentation Dynamique est coché. |
| `dynamic_color_depth` | `bool` | Active la commutation dynamique de la profondeur de couleur à chaud ($8 \leftrightarrow 7 \leftrightarrow 6 \leftrightarrow 5 \leftrightarrow 4 \leftrightarrow 3 \leftrightarrow 2$) sous extinction matérielle OE lors des transitions inter-moteurs (`true` par défaut sur ESP32, Recommandé pour ESP32). Évalue dynamiquement les besoins mémoire du moteur entrant : les moteurs graphiques (`clock`, `fighter`, `gif`, `visualizer`, `message`, `marquee`) s'exécutent en qualité maximale (8 bits), tandis que les moteurs réseau avec TLS (`crypto`, `stock`, `weather`, `gnews`) basculent temporairement en 4 bits lors des requêtes HTTPS pour libérer 16 à 24 Ko de RAM DMA contiguë dans la Sandbox Mémoire. Dès que les cotations et graphiques sont en cache (`needsTlsFetch() == false`), le moteur court-circuite le passage en 4 bits et s'active directement en **8 bits avec transition instantanée à 0 ms (zéro coupure DMA)**. Dans l'UI web, cocher cette option grise automatiquement le sélecteur manuel de profondeur de couleur. Voir [MEMORY_MODEL_FR.md](MEMORY_MODEL_FR.md). |
| `limit_refresh_rate_hz` | `int` | Limite le taux de rafraîchissement (`0` = illimité). |
| `row_address_mode` | `int` | Type d'adressage des lignes (`0`: Direct Binaire, `1`: ShiftReg, `2`: Direct 16, `3`: Direct 32, `4`: Direct 64). |
| `clk_phase` | `bool` | Inverse le front d'horloge CLK (`false` par défaut ; mettre à `true` si la dalle requiert un front inversé). |
| `latch_blanking` | `int` | Nombre de cycles de masquage de latch (`0`–`8`) pour supprimer le ghosting (lignes fantômes). |
| `render_pipeline` | `String` | Pipeline de rendu et de buffering (`auto`, `canvas_single`, `canvas_double`, `direct_double`, `direct_single`). Défaut `auto`. Résout automatiquement le meilleur agencement mémoire, le placement du canvas et la synchronisation DMA. |
| `rotation_offset` | `int` | Décalage physique de montage (`0`=0°, `1`=90°, `2`=180°, `3`=270°). |
| `auto_rotate` | `bool` | Active l'orientation automatique via le gyroscope/IMU embarqué (`true` par défaut). |
| `rotation_transition` | `String` | Effet visuel de transition (`vortex`, `glitch`, `slide`, `zoom`, `matrix`, `random`, `none`). |
| `rotation_transition_duration_ms` | `int` | Durée de l'effet de transition en millisecondes (défaut `400`). |
| `slot_transition` | `String` | Effet joué lorsque la rotation passe à l'écran suivant (`wipe`, `curtain`, `shutter`, `dissolve`, `checker`, `matrix`, `vortex`, `glitch`, `slide`, `zoom`, `random`, `none`). Par défaut `none`. |
| `slot_transition_duration_ms` | `int` | Durée de la transition entre écrans, en millisecondes (`100`-`3000`, par défaut `500`). |

### 2.1 Options du Pipeline de Rendu & Buffering (`render_pipeline: auto`)

ArcadeMatrix v4 introduit le SPI graphique matériellement agnostique (`IDrawingSurface`), découplant la rasterisation des pixels des contrôleurs DMA physiques. Vous pouvez configurer le pipeline directement dans `config.json` ou depuis l'onglet **Paramètres Système → Matériel** de la Web UI :

- **`auto`** *(Recommandé)* : Résout automatiquement le pipeline optimal selon le profil matériel et le palier de mémoire vive :
  - **ESP32-S3 / Cartes avec PSRAM** : Alloue un canvas intermédiaire 16 bits RGB565 en PSRAM externe avec double buffer DMA (`canvas_double`) pour un débit maximal et une animation 60 FPS sans déchirement.
  - **ESP32 Classique (Sans PSRAM)** : Résout vers `canvas_single` avec **Pré-allocation Anticipée du Canvas en SRAM1**. Le canvas hors-écran de 8 Ko est pré-alloué dès l'étape 2 du boot (chargement config, avant que le Wi-Fi et le serveur Web n'entament la SRAM1 `0x3ffb...`). En plaçant le canvas dans la RAM CPU interne, exactement **8 192 octets de mémoire contiguë DMA dans la SRAM2 (`0x3ffe...`) sont sanctuarisés en permanence**. Cela garantit la marge nécessaire au framebuffer HUB75 DMA et aux flux TLS, tout en éliminant le déchirement d'image grâce aux transferts par rafale synchronisés de `Hub75BulkEncoder`.
- **`canvas_single`** : Canvas 16 bits RGB565 + simple buffer DMA. Divise par deux la RAM DMA requise tout en utilisant `Hub75BulkEncoder` pour éliminer le déchirement d'écran.
- **`canvas_double`** : Canvas 16 bits RGB565 + double buffer DMA. Idéal pour les dalles larges (128×64, 256×64) avec PSRAM.
- **`direct_double`** : Rendu direct hérité dans les doubles buffers HUB75 DMA sans canvas intermédiaire.
- **`direct_single`** : Rendu direct hérité dans un simple buffer DMA (empreinte mémoire minimale absolue ; risque de déchirement visible pendant le tracé).

> [!NOTE]
> **Pourquoi l'Auto Depth & l'Auto Buffer assurent 100% de compatibilité :**
> L'ESP32 classique ne dispose que de ~320 Ko de RAM totale, morcelée entre SRAM1 CPU-only (~72 Ko) et SRAM2 compatible DMA (~128 Ko). Faire cohabiter un décodeur `AnimatedGIF` (24 Ko contigus), des flux réseau chiffrés TLS (~33 Ko de tampons SSL/mbedTLS) et une dalle 128×32 en 8 bits (32 Ko de framebuffer DMA) relèverait de l'impossible sans cette architecture.
> 
> La synergie entre l'**Auto Buffer** (déport du canvas de 8 Ko en SRAM1) et l'**Auto Depth** (passage dynamique $8 \leftrightarrow 4$ bits avec conscience du cache) dégage une sandbox mémoire de plus de **46 Ko de bloc contigu libre** lors des besoins réseau. Dès que les données sont téléchargées et mises en cache, l'affichage retrouve instantanément sa splendeur en **8 bits (transition 0 ms sans coupure)** !

### 2.2 Intégration dans l'Onglet Matériel de la Web UI

L'interface Web (Paramètres Système → Matériel) pilote directement ces options :
1. **Menu déroulant Pipeline de Rendu & Buffering (`hw-render-pipeline`)** : Sélection entre `auto`, `canvas_single`, `canvas_double`, `direct_double` ou `direct_single`.
2. **Enregistrement** : Cliquer sur **Sauvegarder la configuration matérielle** (`btn-save-hw`) transmet les paramètres à `POST /api/system`, puis redémarre proprement le panneau avec le nouveau pipeline.

> La luminosité de jour en direct **n'est pas** stockée dans ce bloc ; elle est contrôlée à l'exécution depuis la Web UI (curseur du Dashboard → `POST /api/system { "brightness_limit": 0-100 }`). La luminosité de nuit se trouve dans le bloc `system` (§4).

---

## 3. Le bloc `"wifi"`

| Clé | Type | Description |
| :--- | :--- | :--- |
| `ssid` | `String` | Le nom de votre réseau Wi-Fi (2.4 GHz). |
| `password` | `String` | La clé WPA2. |
| `hostname` | `String` | Nom d'hôte de l'appareil annoncé via mDNS / DHCP (`arcadematrix` par défaut). |

Si `ssid` est vide ou si les identifiants échouent à se connecter, ArcadeMatrix démarre automatiquement un SoftAP Captive Portal nommé `ArcadeMatrix-Setup` (IP : `192.168.4.1`) permettant une configuration immédiate depuis un smartphone ou un ordinateur.

Vous pouvez aussi pousser de nouveaux identifiants Wi-Fi à l'exécution avec `POST /api/wifi { "ssid": "...", "password": "..." }`, ce qui les enregistre dans le stockage persistant et reconnecte le système.

---

## 4. Le bloc `"system"` (environnement et veille)

| Clé | Type | Description |
| :--- | :--- | :--- |
| `timezone` | `String` | Chaîne POSIX (ex. `CET-1CEST,M3.5.0,M10.5.0/3`). |
| `format_24h` | `bool` | Format de l'heure. `true` = 23:00, `false` = 11:00 PM. |
| `lang` | `String` | Langue du système (ex. `en`, `fr`, `es`). |
| `temp_unit` | `String` | Unité de température préférée (`C` pour Celsius, `F` pour Fahrenheit). |
| `temp_offset` | `float` | Décalage d'étalonnage appliqué aux capteurs environnementaux (dans l'unité de température configurée). |
| `night_mode_enabled` | `bool` | Active l'extinction automatique / la réduction de luminosité la nuit. |
| `turn_off_at` | `String` | Heure de début de la veille (ex. `"23:00"`). |
| `wake_up_at` | `String` | Heure de réveil (ex. `"07:00"`). |
| `night_brightness` | `int` | Luminosité de veille (`0` = matrice complètement éteinte). |
| `day_brightness` | `int` | Luminosité de jour en direct (`0`–`100`). Réglée via le curseur du tableau de bord et conservée après un redémarrage. |
| `idle_fighter_enabled` | `bool` | Interrupteur principal de l'overlay Combattant décoratif superposé aux écrans de rotation en veille (activation par écran via chaque entrée de rotation). |
| `idle_fighter_interval` | `int` | Secondes entre deux animations de combat (minimum `1`). |

---

## 5. Le bloc `"mqtt"` (marquees Recalbox / Batocera / RetroPie & Showcase en Jeu)

| Clé | Type | Défaut | Description |
| :--- | :--- | :--- | :--- |
| `enabled` | `bool` | `false` | Active l'écouteur MQTT (`FrontendSync`) pour les marquees Pixelcade et événements console. |
| `broker` | `String` | `"127.0.0.1"` | IP/hôte du broker MQTT local ou de repli (`127.0.0.1` active le broker intégré à l'ESP32 en mode autonome). |
| `port` | `int` | `1883` | Port du broker. |
| `user` | `String` | `""` | Nom d'utilisateur du broker (optionnel). |
| `pass` | `String` | `""` | Mot de passe du broker (optionnel). |
| `device_name` | `String` | `"ArcadeMatrix"` | Identifiant publié par cet appareil. |
| `allow_overlay` | `bool` | `false` | Autorise l'overlay décoratif (ex. Fighter Engine) sur les écrans MQTT/marquees. |
| `use_showcase_broker` | `bool` | `true` | Lorsque `showcase.enabled` vaut `true` et que Showcase est joignable, utilise automatiquement le broker MQTT de Showcase et coupe le broker interne de l'ESP32 pour libérer la RAM interne (bascule automatiquement sur le broker local si Showcase s'éteint). |
| `ingame_showcase_enabled` | `bool` | `true` | Au lancement d'un jeu (`playing`) lorsque Showcase est actif, affiche immédiatement le marquee local (`0 ms`), prépare sur le créneau isolé `{dmd_id}_sync` la Fiche Info + Panoramas + Vidéo Gameplay du jeu (avec basename ROM `query` + repli automatique sur le `title` console `fallback_query`, timeout de préparation de `120 s`), bascule en douceur dès que la Frame 0 est prête, et interrompt automatiquement les processus de conversion serveur via `DELETE /api/v1/clients/{sync_id}/queue` à la sortie du jeu (`0` requête HTTP pendant la navigation menu). |
| `ingame_cycle_mode` | `String` | `"once"` | Comportement pendant la partie : `"once"` (joue le Showcase une fois puis garde le marquee), `"alternating"` (alterne Showcase et pause sur le marquee) ou `"loop"` (boucle le Showcase en continu). |
| `ingame_marquee_interval_sec` | `int` | `45` | Durée d'affichage du marquee fixe (en secondes) entre deux cycles Showcase lorsque `ingame_cycle_mode` vaut `"alternating"` (`5`–`600`). |
| `card_style_source` | `String` | `"showcase_profile"` | `"showcase_profile"` hérite du profil visuel configuré dans Showcase Studio ; `"custom"` applique les réglages Carte & Vidéo personnalisés ci-dessous directement depuis le DMD. |
| `include_card_intro` | `bool` | `true` | (Mode `custom`) Inclut l'introduction Fiche Info animée avant les panoramas et la vidéo. |
| `card_layout` | `String` | `"classic_split"` | (Mode `custom`) Mise en page de la fiche : `"classic_split"`, `"badge_header"`, `"full_art"` ou `"minimal"`. |
| `card_theme` | `String` | `"tinted"` | (Mode `custom`) Thème visuel de la fiche : `"tinted"`, `"neon_border"` ou `"poster_wash"`. |
| `show_poster` | `bool` | `true` | (Mode `custom`) Affiche la mini-jaquette sur la Fiche Info. |
| `show_synopsis` | `bool` | `true` | (Mode `custom`) Fait défiler le synopsis localisé du jeu sur la Fiche Info. |
| `text_scroll_speed` | `String` | `"4"` | (Mode `custom`) Vitesse de défilement du synopsis (`"1"` Lent à `"10"` Turbo, ou `"slow"` / `"normal"` / `"fast"`). |
| `jacket_panorama` | `bool` | `true` | (Mode `custom`) Panorama vertical de la jaquette complète après la Fiche Info. |
| `marquee_panorama` | `bool` | `false` | (Mode `custom`) Panorama vertical de la bannière marquee arcade. |
| `snapshot_panorama_count` | `int` | `0` | (Mode `custom`) Nombre de captures d'écran de jeu en panorama (`0`–`5`). |
| `show_video` | `bool` | `true` | (Mode `custom`) Diffuse l'extrait vidéo de gameplay après la fiche et les panoramas. |
| `video_max_duration_s` | `int` | `12` | (Mode `custom`) Durée maximale de la vidéo de gameplay en secondes (`2`–`120`). |
| `video_fps` | `int` | `15` | (Mode `custom`) Fréquence cible du flux vidéo (`8`–`60` FPS). |
| `video_mode` | `String` | `"auto_action"` | (Mode `custom`) Cadrage vidéo : `"auto_action"`, `"panorama"` ou `"center_crop"`. |
| `color_boost` | `String` | `"auto"` | (Mode `custom`) Profil colorimétrique LED HUB75 : `"auto"`, `"pixel_art"`, `"anime"` ou `"cinema"`. |
| *(auto-souscription)* | `system/playing/#` | — | Souscrit automatiquement à tous les systèmes rétro compatibles : `system/playing/recalbox`, `system/playing/batocera`, `system/playing/retropie`. |

### Découverte Dynamique du Broker, Isolation du Slot Sync & Abandon Déterministe des Processus
- **Zéro Réinstallation des Scripts (`GET /api/mqtt/broker-target`)** : Les scripts console installés via `tools/recalbox_daemon/` ne stockent que l'IP de votre DMD (`DMD_HOST`) et interrogent dynamiquement `GET /api/mqtt/broker-target` (avec repli automatique sur `<DMD_HOST>:1883`). Activer ou éteindre Showcase ne nécessite jamais de réinstaller les scripts sur Recalbox, Batocera ou RetroPie.
- **Créneau `{dmd_id}_sync` Isolé, Repli Automatique sur `title` & Arrêt des Processus Serveur (`DELETE /api/v1/clients/{sync_id}/queue`)** : Au lancement d'un jeu (`status: "playing"`), `FrontendSync` transmet à la fois le nom de ROM (`game` → `query`) et le titre lisible du jeu sur la console lorsqu'il diffère (`title` → `fallback_query`) avec `mode: "play_now"` et `mark_seen: false` vers un créneau éphémère dédié `esp32_<MAC6>_games_<WxH>_sync` (timeout de préparation de `120 s`), sans jamais perturber la rotation autonome de `retro_games`. La navigation dans les menus (`browsing`) n'émet **`0` requête HTTP** vers Showcase ; quitter une partie (`browsing`/`stopped`), lancer un autre jeu, atteindre l'échéance de `120 s` ou terminer un cycle `once` envoie automatiquement `DELETE /api/v1/clients/{sync_id}/queue` (`SO_LINGER {1, 0}`) depuis le Core 0 afin d'annuler la file et de tuer immédiatement tous les arbres de processus workers actifs sur le serveur (`dmd_show_converter`, `ffmpeg`, `yt-dlp`).
- **Purge du Cache Showcase en 1 Clic (`POST /api/showcase/cache/evict`)** : Dans **Système → MQTT & API → 🎮 FrontendSync** (et **🎬 DMD Media Showcase**), le bouton **🗑️ Vider le Cache Showcase** vide immédiatement le cache persistant des conversions On-Demand sur Showcase lorsque vous changez de thème ou de mise en page.

> [!NOTE]
> Pour Batocera, la version **v33 ou supérieure** est requise pour le changement dynamique de marquee pendant la navigation (hooks `game-selected` et `system-selected`). Batocera v32 et versions antérieures ne déclenchent que les événements de lancement/arrêt de jeu. Recalbox est supporté sur toutes ses versions.

---

## 5.1 Le bloc `"data_mqtt"` (broker Données MQTT)

Le broker auquel le moteur `mqttdata` se connecte **pendant qu'il est affiché**. Uniquement des identifiants : rien ne se connecte ni ne tourne en arrière-plan, et c'est distinct de la liaison arcade `"mqtt"` ci-dessus (qui peut prendre la main sur l'affichage).

| Clé | Type | Défaut | Description |
| :--- | :--- | :--- | :--- |
| `broker` | `String` | `""` | IP/hôte du broker. |
| `port` | `int` | `1883` | Port du broker. |
| `user` | `String` | `""` | Nom d'utilisateur du broker (optionnel). |
| `pass` | `String` | `""` | Mot de passe du broker (optionnel). |

Web UI : *Système → MQTT & API → Données MQTT*. Clés plates sur `/api/settings` : `data_mqtt_broker`, `data_mqtt_port`, `data_mqtt_user`, `data_mqtt_pass` ; objet `data_mqtt` imbriqué sur `/api/system`. L'identifiant client est `<hostname wifi>-data`. Voir [MQTT_DATA_CONTRACT_FR.md](MQTT_DATA_CONTRACT_FR.md) pour le protocole et [HOME_ASSISTANT_FR.md](HOME_ASSISTANT_FR.md) pour Home Assistant.

---

## 6. Sécurité de l'API (`api_auth_enabled` / `api_token`)

Ces paramètres déterminent si les points d'entrée (endpoints) mutatifs et administratifs (redémarrages, réglages de luminosité, sauvegarde de configuration, modifications des moteurs, mises à jour OTA et téléversement de GIFs) exigent un jeton d'authentification secret valide.

| Clé | Type | Valeur par défaut | Description |
| :--- | :--- | :--- | :--- |
| `api_auth_enabled` | `bool` | `false` | Si `false`, tous les endpoints sont accessibles librement sans mot de passe. Si `true`, les endpoints sensibles requièrent un jeton valide. |
| `api_token` | `String` | `""` | Le jeton secret d'authentification. En écriture seule (*write-only*) : jamais exposé par les requêtes `GET` (qui renvoient `api_token_configured: true`). |

> [!NOTE]
> Les endpoints publics de télémétrie en lecture seule (`/api/status`, `/api/metrics`, `/api/health`, `/api/i18n/current`) restent toujours accessibles sans authentification, garantissant le fonctionnement continu de vos dashboards et sondes de supervision.

### 6.1 Comment Activer ou Désactiver l'Authentification

#### Option A : Via l'Interface Web (Recommandé)
1. Ouvrez la WebUI dans votre navigateur (`http://arcadematrix.local` ou l'adresse IP de votre appareil).
2. Rendez-vous dans l'onglet **Système** (`sys-tab-hardware`) et repérez la carte **Authentification API**.
3. Réglez **Exiger l'authentification API** sur **Activé** (`Enabled`).
4. Saisissez le jeton secret de votre choix dans le champ **Jeton API (Token)**.
5. Cliquez sur **Enregistrer l'auth API**. Le jeton est enregistré immédiatement dans `/config.json` sur la carte SD et mis en cache automatiquement dans le `localStorage` de votre navigateur.
6. Pour **désactiver** l'authentification à tout moment, passez simplement le sélecteur sur **Désactivé** (`Disabled`) et cliquez sur **Enregistrer l'auth API**.

#### Option B : Via le fichier `config.json` sur la carte SD
Éditez directement `/config.json` et configurez le bloc `system` :
```json
{
  "system": {
    "api_auth_enabled": true,
    "api_token": "mon_super_token_secret_123"
  }
}
```

### 6.2 Comment Créer un Jeton Sécurisé
Vous pouvez utiliser n'importe quelle chaîne alphanumérique. Pour une sécurité optimale, générez un jeton aléatoire de 32 caractères hexadécimaux dans votre terminal :
```bash
# Sous Linux / macOS :
openssl rand -hex 16
# Exemple de résultat : a3f89e2b109c4d5e7812bc34567890ef
```
Vous pouvez également choisir une phrase secrète robuste facile à retenir.

### 6.3 Comment se Connecter à la WebUI en Tant Que Nouvel Utilisateur

Lorsque la sécurité API est activée (`api_auth_enabled: true`), un nouvel utilisateur ou un nouveau navigateur accède à l'appareil de manière transparente et intuitive :

1. **Accès Initial Libre à la WebUI** :
   L'interface web (`GET /`, `GET /index.html`) et les endpoints de consultation en lecture seule (télémétrie, état matériel, horloge) restent accessibles librement. La page se charge donc normalement sur n'importe quel nouvel appareil.

2. **Trois Façons d'Authentifier un Nouveau Navigateur** :

   * **Option 1 : Automatique lors de la 1ère Action (Boîte de dialogue interactive)**
     Dès que le nouvel utilisateur tente une action modifiant l'état (changer la luminosité, basculer d'horloge, modifier une couleur, sauvegarder un réglage), l'ESP32 retourne une réponse `401 Unauthorized`.
     L'interface Web intercepte automatiquement ce code 401 et affiche une invite de saisie (*prompt*) :
     > `Jeton API requis ou invalide. Entrez votre jeton API :`
     L'utilisateur colle son jeton secret. L'UI le mémorise immédiatement dans le `localStorage` du navigateur (`api_token`). Toutes les actions suivantes sont alors automatiquement autorisées sans plus jamais rien ressaisir.

   * **Option 2 : Saisie Préalable dans les Paramètres Système**
     Sans attendre une erreur 401, l'utilisateur peut se rendre directement dans :
     **Système** ➔ onglet **Paramètres** ➔ carte **Authentification API**.
     Il lui suffit de saisir le jeton dans le champ **Jeton API (Token)** et de cliquer sur **Enregistrer l'auth API**. Le jeton est enregistré dans le navigateur et validé auprès de l'appareil.

   * **Option 3 : Lien Direct / Favori URL (*Magic Link*)**
     Un administrateur peut partager ou mettre en favori une URL intégrant le paramètre `?token=` :
     `http://arcadematrix.local/?token=mon_super_token_secret_123`
     Dès l'ouverture de la page, la WebUI extrait automatiquement le paramètre `token` et le stocke dans le `localStorage` du navigateur. L'utilisateur est instantanément authentifié pour toute sa navigation future, sans aucune boîte de dialogue requise.

* **Sécurité Écriture Seule (*Write-Only*)** : L'ESP32 ne renvoie jamais le jeton secret lors des requêtes `GET /api/system` (il indique seulement `"api_token_configured": true`), éliminant tout risque d'espionnage réseau.
* **Modification ou Suppression du Jeton** : Si un jeton erroné a été saisi, soumettre le jeton valide dans l'invite ou dans les Paramètres écrase l'ancienne valeur. Vider le champ et enregistrer désactive l'authentification.

### 6.4 Appel de l'API REST depuis des Scripts Externes ou Home Assistant
Lorsque `api_auth_enabled` est `true`, les clients externes s'authentifient via les en-têtes HTTP standards ou un paramètre d'URL :

1. **Recommandé : En-tête `X-API-Token`**
   ```bash
   curl -X POST http://arcadematrix.local/api/system/brightness \
     -H "Content-Type: application/json" \
     -H "X-API-Token: mon_super_token_secret_123" \
     -d '{"brightness": 80}'
   ```

2. **En-tête Bearer Standard (`Authorization`)**
   ```bash
   curl -X POST http://arcadematrix.local/api/system/restart \
     -H "Authorization: Bearer mon_super_token_secret_123"
   ```

3. **Paramètre Query d'URL (Tests Rapides / Raccourcis)**
   ```bash
   curl -X POST "http://arcadematrix.local/api/system/restart?token=mon_super_token_secret_123"
   ```

### 6.5 Protection Anti-Verrouillage (*Anti-Lockout*)
Afin de ne jamais vous retrouver bloqué hors de votre horloge :
* Si `api_auth_enabled` est réglé sur `true` mais que `api_token` est vide (`""`), le firmware considère la protection comme inactive et autorise l'accès.
* La vérification du jeton s'effectue en temps constant (`TimingSafe::compare`), éliminant les attaques par mesure de latence réseau (*timing attacks*).

---


## 7. Moteurs : `"instances"` & `"rotation"`

L'architecture découplée permet de créer plusieurs copies indépendantes, configurées différemment, du même Engine.

### `"instances"`
Un tableau contenant la configuration de chaque bloc logique.

```json
{
  "instance_id": "crypto_main",
  "engine_id": "crypto",
  "config": {
    "symbols": "BTC,ETH,SOL"
  }
}
```
* `instance_id` : Nom unique de ce bloc.
* `engine_id` : Identifiant interne du moteur C++ (`IDisplayEngine`, doit être un moteur enregistré — voir §9).
* `config` : Une map plate de valeurs `String` propres au moteur, validée par rapport à son `ConfigSchema`.

Modifier une instance via la Web UI (`POST /api/instances`) est appliqué **à chaud, sans redémarrage** : le runtime appelle `on_config_changed()` du moteur à la frame suivante (hot-reload Lazy-Once). Ajouter ou supprimer une instance réinitialise proprement la rotation.

### `"rotation"`
Définit l'ordre d'affichage, la durée de chaque créneau et l'activation des overlays transversaux.

```json
{
  "instance_id": "crypto_main",
  "duration_sec": 30,
  "overlays": {
    "fighter": true
  }
}
```
* `instance_id` : Nom de l'instance ciblée.
* `duration_sec` : Durée d'affichage en secondes (ou quota de lecture pour les moteurs autonomes comme GIF).
* `overlays.fighter` : (`bool`) Interrupteur granulaire pour l'overlay décoratif de combat M.U.G.E.N sur cet écran précis.

Seules les instances listées ici sont initialisées, ce qui économise de la mémoire pour les fonctionnalités inutilisées. La rotation est modifiable depuis le panneau **Rotation** de la Web UI (`GET`/`POST /api/rotation`).

> **Persistance Lisible** : Lors de son enregistrement sur disque (`config.json`), le fichier est systématiquement écrit formaté avec des indentations claires (`serializeJsonPretty`) pour permettre une relecture et modification manuelle sans risque.

---

### Options d'une instance d'horloge

Outre `clock_theme`, `clock_font`, `clock_size` et les couleurs, une instance d'horloge accepte :

| Clé | Type | Description |
| :--- | :--- | :--- |
| `clock_glow` | `enum` | Contour autour des chiffres : `0` aucun, `1` néon (la couleur choisie à pleine intensité avec un centre presque blanc, comme le fait la face Matrix), `2` une couleur dédiée. Par défaut `0`. |
| `clock_glow_color` | `color` | Couleur du contour lorsque `clock_glow` vaut `2` (par défaut `#00FF41`). |

Les thèmes `30` à `34` et `37` sont des faces adaptées des clockfaces Clockwise. Celles marquées
`(256x64)` sont conçues pour un panneau large et affichent un avertissement sur les petits panneaux.
---

## 8. Validation autoréparatrice

À chaque démarrage **et** à chaque écriture via `POST /api/instances`, le `ConfigSanitizer` réconcilie chaque instance avec le `ConfigSchema` de son moteur :

* **Clé manquante** → le `default_value` du schéma est injecté.
* **Integer / Float** → analysé puis, si hors `min`/`max`, borné ou réinitialisé à la valeur par défaut (selon le `validation_policy` du champ).
* **Boolean** → normalisé (`true/1/yes/on` → `true`, `false/0/no/off` → `false`) ; une valeur impossible à analyser revient à la valeur par défaut.
* **Options** → la valeur doit faire partie des options déclarées (liste séparée par des virgules pour la sélection multiple) ; sinon elle revient à la valeur par défaut.
* **Clés obsolètes** → les clés qui ne sont plus présentes dans le schéma (ex. après une OTA qui a renommé un champ) sont supprimées.

Le résultat est enregistré atomiquement, donc une OTA qui ajoute un nouveau champ le renseigne automatiquement sans intervention utilisateur.

---

## 9. Configurations des moteurs

Chaque moteur expose ses propres champs via son `ConfigSchema` (consultable à `GET /api/engines`, qui alimente la Web UI dynamique). Les moteurs les plus courants :

### Moteur : `clock`
| Champ | Type | Défaut | Description |
| :--- | :--- | :--- | :--- |
| `theme` | `int` | `0` | Index du thème d'horloge : `0` = Digital Standard, `1` = Flip Clock, `2` = Cyberpunk, `3` = Word Clock, `4` = Binary Clock, `5` = Pac-Man, `6` = Tetris, `7` = Slot Machine, `8` = Versus (M.U.G.E.N), `9` = Pong, `10` = Matrix Rain (Katakana). |
| `format` | `String` | `%H:%M:%S` | Format d'heure strftime. |
| `font` | `String` | `Default` | Police bitmap intégrée (`Default`) ou fichier de police depuis `/fonts/`. |
| `size` | `int` | `2` | Facteur de mise à l'échelle de la police. |
| `color_1` | `String` | `#FFFFFF` | Couleur hex principale (début du dégradé sur le thème Custom). |
| `color_2` | `String` | `#FFFFFF` | Couleur hex secondaire (fin du dégradé sur le thème Custom). |
| `offset_x` | `int` | `0` | Décalage horizontal en pixels. |
| `offset_y` | `int` | `0` | Décalage vertical en pixels. |

### Moteur : `date`
| Champ | Type | Défaut | Description |
| :--- | :--- | :--- | :--- |
| `theme` | `int` | `0` | Index du thème de date. |
| `format` | `String` | `%d/%m` | Format de date strftime. |
| `font` | `String` | `Default` | Police bitmap intégrée (`Default`) ou fichier de police depuis `/fonts/`. |
| `size` | `int` | `2` | Facteur de mise à l'échelle de la police. |
| `color_1` | `String` | `#FFFFFF` | Couleur hex principale. |
| `color_2` | `String` | `#FFFFFF` | Couleur hex secondaire. |
| `offset_x` | `int` | `0` | Décalage horizontal en pixels. |
| `offset_y` | `int` | `0` | Décalage vertical en pixels. |

### Moteur : `crypto`
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `symbols` | `String` | `BTC,ETH` | Séparés par virgule | Symboles cryptos à surveiller (CoinGecko / Binance). |
| `show_chart` | `bool` | `true` | `true`, `false` | Afficher la courbe sparkline historique. |
| `chart_timeframe` | `Options` | `daily` | `hourly`, `daily`, `weekly`, `monthly` | Échelle de temps pour l'historique des cours. |
| `duration_sec` | `int` | `5` | `2` à `60` | Secondes d'affichage par page (alias `page_seconds`). Le moteur `crypto` étant autonome (`selfPaced() == true`), ce temps par page pilote la durée complète du créneau de rotation afin de faire défiler l'intégralité des cryptos configurées avant de passer à l'écran suivant. |
| `cache_ttl_min` | `int` | `1` | `1` à `60` | Minutes de rétention du cache de cotation. |

### Moteur : `stock`
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `symbols` | `String` | `AAPL,NVDA,TSLA` | Séparés par virgule | Tickers boursiers à surveiller (Yahoo Finance). |
| `show_chart` | `bool` | `true` | `true`, `false` | Afficher la courbe sparkline historique. |
| `chart_timeframe` | `Options` | `daily` | `hourly`, `daily`, `weekly`, `monthly` | Échelle de temps pour l'historique des cours. |
| `duration_sec` | `int` | `5` | `2` à `60` | Secondes d'affichage par page (alias `page_seconds`). Le moteur `stock` étant autonome (`selfPaced() == true`), ce temps par page pilote la durée complète du créneau de rotation afin de faire défiler l'intégralité des actions configurées avant de passer à l'écran suivant. |
| `cache_ttl_min` | `int` | `1` | `1` à `60` | Minutes de rétention du cache de cotation. |

### Moteur : `weather`
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `api_key` | `String` | `""` | Clé API gratuite | Clé API OpenWeatherMap (création gratuite sur [openweathermap.org](https://home.openweathermap.org/users/sign_up)). |
| `city` | `String` | `""` | Texte | Ville pour les prévisions météo (voir guide ci-dessous). |
| `units` | `Options` | `metric` | `metric`, `imperial` | Unité de température : `metric` pour Celsius (°C) ou `imperial` pour Fahrenheit (°F). Les températures portent le signe degré (« 90°F ») ; en °F, la maximale s'affiche au-dessus de la minimale (convention américaine). |
| `lang` | `Options` | `en` | `en`, `fr`, `es` | Langue d'affichage des jours (TODAY / AUJ. / HOY). |
| `offset_x` | `int` | `0` | `-64` à `64` | Décalage horizontal en pixels. |
| `offset_y` | `int` | `0` | `-32` à `32` | Décalage vertical en pixels. |

#### Formatage du champ `city` sur OpenWeatherMap
OpenWeatherMap utilise le code pays ISO 3166 (et le code d'état à 2 lettres pour les États-Unis) :
* **Emplacements Internationaux :** Utilisez `Ville,CodePays` (ex. `Paris,FR`, `London,GB`, `Tokyo,JP`, `Montreal,CA`).
* **Emplacements aux États-Unis :** Utilisez `Ville,CodeEtat,CodePays` (ex. `Tucson,AZ,US`, `Miami,FL,US`, `Dallas,TX,US`).
* **Où trouver le nom exact :** Rendez-vous sur [openweathermap.org](https://openweathermap.org) et recherchez votre ville.

### Moteur : `system_info` (Moniteur Système)
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `theme` | `int` | `0` | `0` à `2` | Thème visuel : `0` = Jauges horizontales colorées, `1` = Grille compacte 4 blocs, `2` = Terminal Retro. |
| `show_cpu` | `bool` | `true` | `true`, `false` | Affiche l'utilisation instantanée du processeur (CPU %). |
| `show_ram` | `bool` | `true` | `true`, `false` | Affiche l'utilisation de la mémoire vive (RAM %). |
| `show_temp` | `bool` | `true` | `true`, `false` | Affiche la température matérielle du SoC (vert/orange/rouge dynamique). |
| `show_uptime` | `bool` | `true` | `true`, `false` | Affiche le temps d'activité (Uptime) en heures/jours. |
| `temp_unit` | `Options` | `C` | `C`, `F` | Unité de température : Celsius (`C`) ou Fahrenheit (`F`). |
| `offset_x` | `int` | `0` | `-64` à `64` | Décalage horizontal en pixels. |
| `offset_y` | `int` | `0` | `-32` à `32` | Décalage vertical en pixels. |

### Moteur : `gnews` (Actualités en Direct & Ticker GNews)

Le moteur `gnews` affiche un bandeau d'actualités et d'alertes en temps réel alimenté par l'API [GNews.io](https://gnews.io). Il intègre un pool multi-clés d'API avec bascule automatique, une persistance sur carte SD/disque et une gestion optimisée des quotas journaliers.

| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `api_key` | `String` | `""` | Clés séparées par virgules | Clés API GNews.io (supporte plusieurs clés : `cle1,cle2,cle3` pour pool multi-comptes avec bascule automatique). |
| `category` | `Options` | `technology` | `general`, `world`, `nation`, `business`, `technology`, `entertainment`, `sports`, `science`, `health` | Catégorie thématique principale ou liste séparée par des virgules pour rotation. |
| `keywords` | `String` | `""` | Texte / Requête | Mots-clés de recherche ou tags personnalisés (ex. `ai OR arcade`). |
| `lang` | `Options` | `auto` | `auto`, `en`, `fr`, `es`, `de`, `it`, `pt`, `nl`, `ru`, `zh`, `ja` | Langue des articles (`auto` synchronise avec la langue système). |
| `country` | `Options` | `auto` | `auto`, `us`, `fr`, `gb`, `es`, `de`, `ca`, `it`, `jp`, `au`, `br`, `in` | Édition régionale du pays. |
| `max_articles` | `int` | `5` | `3` à `15` | Nombre maximal d'articles mis en cache et tirés aléatoirement sans répétition à travers les cycles de rotation. |
| `articles_per_rotation` | `int` | `2` | `1` à `15` | Rotation auto-cadencée : nombre d'articles aléatoires à faire défiler intégralement (`PauseStart` -> `Scrolling` -> `PauseEnd`) avant de passer à l'écran suivant. |
| `requests_per_day` | `int` | `10` | `1` à `100` | Budget total de requêtes API alloué par 24 heures (offre gratuite GNews : 100 requêtes/jour/clé). |
| `force_refresh` | `bool` | `false` | `true`, `false` | Action : purge immédiatement le cache de l'ancienne langue et interroge l'API sans réinitialiser les compteurs de quota. |
| `cache_ttl_min` | `int` | `30` | `5` à `120` | Intervalle minimal en minutes entre requêtes quand le budget journalier est détendu. |
| `display_mode` | `Options` | `smooth_scroll` | `smooth_scroll`, `vertical_crawl`, `static_paged`, `serpentine` | Style d'animation (défilement horizontal fluide, défilement vertical, pagination multiligne, ou serpentin alterné). |
| `scroll_speed` | `int` | `3` | `1` à `10` | Vitesse de défilement (1 : Lent à 10 : Turbo). |
| `scroll_pause_start_ms` | `int` | `1200` | `0` à `4000` | Temps de pause fixe (ms) au début de l'article avant le défilement. |
| `scroll_pause_end_ms` | `int` | `1000` | `0` à `4000` | Temps de pause fixe (ms) à la fin de l'article avant la transition. |
| `article_duration_sec` | `int` | `12` | `5` à `60` | Durée d'affichage de repli par article en secondes. |
| `theme` | `Options` | `category_dynamic` | `category_dynamic`, `breaking_crimson`, `cyberpunk`, `monochrome_paper` | Schéma de couleurs visuel. |
| `show_title` | `bool` | `false` | `true`, `false` | Inclut le titre de l'article dans le texte défilant. |
| `show_description` | `bool` | `false` | `true`, `false` | Inclut le résumé / chapeau de l'article dans le texte défilant. |
| `show_content` | `bool` | `true` | `true`, `false` | Inclut le corps du texte de l'article dans le défilement (avec repli automatique sur description/titre si vide). |
| `show_category_badge` | `bool` | `true` | `true`, `false` | Affiche le badge thématique coloré (`[TECH]`, `[WORLD]`, etc.). |
| `show_source` | `bool` | `true` | `true`, `false` | Affiche le nom de la source d'actualités (`BBC News`, `Reuters`, etc.). |
| `show_time_ago` | `bool` | `true` | `true`, `false` | Affiche l'ancienneté relative (`5m ago`, `2h ago`). |
| `show_beacon` | `bool` | `true` | `true`, `false` | Affiche le témoin lumineux de direct clignotant. |
| `show_progress_dots` | `bool` | `true` | `true`, `false` | Affiche les points indicateurs de progression (`● ○ ○ ○ ○`). |

#### Architecture & Optimisation des Quotas GNews
1. **Pool Multi-Clés & Bascule Automatique (Failover) :**
   - Vous pouvez renseigner plusieurs clés API GNews séparées par des virgules (`api_key: "cle1,cle2,cle3"`).
   - Dès qu'une clé est invalide (`HTTP 401/403`) ou épuise son quota de 100 requêtes/jour (`HTTP 429/403`), le moteur bascule instantanément sur la clé suivante et retente la requête.
   - 2 comptes = 200 requêtes/jour ; 3 comptes = 300 requêtes/jour.
2. **Persistance sur Fichier (`/gnews_cache.json` sur SD/SPIFFS) & Lecture Aléatoire :**
   - Les articles et compteurs de télémétrie sont persistés sur stockage physique. Au redémarrage, les titres s'affichent immédiatement sans consommer de quota API ni bloquer l'écran.
   - D'un créneau de rotation à l'autre, les articles en cache sont tirés selon une permutation aléatoire Fisher-Yates sans répétition afin que toutes les actualités en cache défilent équitablement même lors de créneaux de rotation courts.
   - En cas de coupure réseau ou de quota épuisé, les articles en cache sont conservés et continuent de défiler 24h/24.
3. **Budget de Requêtes Journalier (Défaut : 10 reqs/jour) & Préservation des Clés Partagées :**
   - Bien que les comptes gratuits GNews.io autorisent jusqu'à 100 requêtes/jour par clé, les utilisateurs partagent fréquemment leur clé avec d'autres services ou outils domotiques externes.
   - Afin d'éviter qu'ArcadeMatrix ne monopolise ou n'épuise la clé, le moteur adopte par défaut un budget conservateur de **10 requêtes par jour** (`requests_per_day : 10`, espacées régulièrement toutes les 2h24 : $\Delta t = \frac{86400}{10} = 8640\text{ s}$).
   - L'utilisateur peut librement ajuster ce budget de `1` à `100`. L'interface de configuration Web affiche dynamiquement le décompte par rapport au budget choisi (ex : `Clé 1 (..abcd) : 4/10 reqs [Active]`).
   - Si plusieurs catégories sont spécifiées (ex : `technology,world`), les requêtes alternent en round-robin ($\frac{\text{requests\_per\_day}}{N}$ par catégorie).
4. **Prise en Compte Différée & Forçage Immédiat :**
   - Modifier des paramètres dans l'interface Web s'applique au prochain cycle prévu pour éviter de gaspiller le quota.
   - Activer `force_refresh: true` purge les articles de l'ancienne langue et déclenche un appel immédiat tout en préservant le suivi des compteurs journaliers.
5. **Remise à Zéro Automatique à Minuit (00:00 UTC) :**
   - Le changement de jour calendrier réinitialise automatiquement les compteurs d'usage à 0 et lève les alertes de limite de quota.

### Overlay Transversal : `fighter` (Combat M.U.G.E.N)
> [!NOTE]
> `FighterEngine` **n'est pas** un moteur autonome enregistré dans `EngineRegistry` (`instances`). Il opère comme un **overlay décoratif transversal** superposé aux écrans de rotation actifs (`RotationManager` étape 2) lorsqu'il est activé globalement dans le bloc `system` (`idle_fighter_enabled`, `idle_fighter_interval`, `fighter_theme`, `fighter_1`, `fighter_2`, `fighter_stage`) et autorisé par créneau via `rotation[i].overlays.fighter = true`.

| Champ Système | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `idle_fighter_enabled` | `bool` | `true` | `true`, `false` | Interrupteur principal de l'overlay décoratif Combattant. |
| `idle_fighter_interval` | `int` | `5` | `1` à `3600` | Secondes entre deux animations de combat. |
| `fighter_theme` | `int` | `0` | `0`, `1` | Style visuel de l'overlay (`0` = Idle / Street Brawlers, `1` = Arène M.U.G.E.N complète). |
| `fighter_1` | `String` | `Random` | Nom du dossier | Combattant P1 (`Random` pour sélection automatique). |
| `fighter_2` | `String` | `Random` | Nom du dossier | Combattant P2 (`Random` pour sélection automatique). |
| `fighter_stage` | `String` | `None` | Nom de l'arène | Arène de fond optionnelle (`None` pour superposer directement sur le moteur actif). |

### Moteur : `music_player` (Lecteur Musical Universel)
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `show_source` | `bool` | `true` | `true`, `false` | Affiche le badge de la source audio active (Bluetooth A2DP, Web Radio ou Lecteur SD). |
| `show_title` | `bool` | `true` | `true`, `false` | Affiche le titre du morceau ou de la station en cours. |
| `show_artist` | `bool` | `true` | `true`, `false` | Affiche l'artiste ou les métadonnées de l'album. |
| `show_progress` | `bool` | `true` | `true`, `false` | Affiche la barre de progression temporelle en bas de l'écran. |
| `show_visualizer` | `bool` | `true` | `true`, `false` | Affiche le visualiseur de spectre audio en temps réel pendant la lecture. |
| `marquee_speed` | `int` | `40` | `10` à `200` | Vitesse de défilement en millisecondes par pas pour les titres longs. |

### Moteur : `google_cast` (Google Home / Nest Audio)
| Champ | Type | Défaut | Description |
| :--- | :--- | :--- | :--- |
| `device_ip` | `String` | `""` | IP statique de votre enceinte Google Home / Nest Audio. Laissez vide pour la découverte mDNS automatique sur le réseau local. |
| `device_name` | `String` | `""` | Filtre sur le nom de l'appareil (ex. `Salon`) lors de la détection automatique sur le LAN. |
| `show_album_art` | `bool` | `true` | Télécharge et affiche la pochette de l'album à gauche de la matrice. |
| `show_progress` | `bool` | `true` | Affiche la barre de progression temporelle de la lecture en bas de l'écran. |
| `show_visualizer` | `bool` | `true` | Affiche l'égaliseur de fréquences animé quand la musique est en lecture. |
| `show_volume` | `bool` | `true` | Affiche le niveau de volume actuel de l'enceinte Google Nest. |

### Moteur : `spotify` (Lecteur Officiel Spotify)
| Champ | Type | Défaut | Description |
| :--- | :--- | :--- | :--- |
| `client_id` | `String` | `""` | Votre Client ID Spotify Developer API. |
| `client_secret` | `String` | `""` | Votre Client Secret Spotify Developer API (optionnel pour PKCE). |
| `refresh_token` | `String` | `""` | Votre Refresh Token OAuth2 Spotify pour la synchronisation continue de la lecture. |
| `show_album_art` | `bool` | `true` | Télécharge et affiche la pochette d'album Spotify sur la matrice. |
| `show_progress` | `bool` | `true` | Affiche la barre de progression temporelle du morceau en bas de l'écran. |
| `show_visualizer` | `bool` | `true` | Affiche l'égaliseur audio animé quand la musique est en lecture. |
| `show_volume` | `bool` | `true` | Affiche le pourcentage de volume de lecture Spotify actif. |

### Moteur : `gifs`
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `playlists` | `String` (Multi) | `""` | Options depuis `/api/playlists` | Dossiers / Playlists de GIFs actives à faire défiler (séparées par virgule). |

### Moteur : `message`
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `text` | `String` | `Hello` | Texte | Texte du message ou de la bannière à afficher. |
| `color` | `String` | `#ffffff` | Couleur Hex | Couleur du texte au format `#RRGGBB`. |
| `size` | `int` | `1` | `1` à `4` | Multiplicateur d'échelle de la police. |
| `direction` | `Options` | `left` | `left`, `none` | Sens de défilement (`left` pour défilement vers la gauche, `none` pour texte statique centré). |
| `speed` | `int` | `50` | `10` à `200` | Millisecondes par pixel de défilement (plus bas = plus rapide ; ignoré en statique). |
| `font` | `String` | `Default` | Dynamique | Police de caractères depuis `/fonts/`. |

### Moteur : `dashboard` (Master Deck Horizontal & Hub Multi-Widgets)
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `clock_mode` | `Options` | `1` | `0:Digital Modern, 1:Pixel-Art Watch Dial, 2:Minimal` | Style visuel de l'horloge principale. |
| `theme` | `Options` | `0` | `0:Cyberpunk Neon, 1:Arcade Amber HUD, 2:Minimalist Luxury, 3:Matrix Phosphor` | Palette de couleurs des widgets et du cadran. |
| `show_clock` | `bool` | `true` | `true`, `false` | Affiche l'horloge principale. |
| `show_world_clock` | `bool` | `true` | `true`, `false` | Affiche le badge des horloges mondiales. |
| `world_clocks` | `String` (Multi) | `NYC,TYO,LON` | Tags présélectionnés / Saisie libre | Codes de villes/aéroports mondiaux (`NYC`, `TYO`, `LON`, `PAR`, `LAX`, `SFO`, `DXB`, `SIN`, `HKG`, `SYD`, `BER`, `ROM`, `MAD`, `AMS`, `YUL`, `UTC`) ou offsets personnalisés (`REU:+4`, `NYC:-4`). |
| `show_weather` | `bool` | `true` | `true`, `false` | Affiche la météo et la température extérieure. |
| `weather_city` | `String` | `Paris` | Texte | Ville pour les prévisions météo. |
| `weather_api_key` | `String` | `""` | Clé API Optionnelle | Clé API OpenWeatherMap (laisser vide pour basculer automatiquement sur le service gratuit Open-Meteo sans clé). |
| `show_indoor_temp` | `bool` | `true` | `true`, `false` | Affiche la température et humidité intérieure du capteur SHTC3. |
| `temp_unit` | `Options` | `system` | `system:System (Général), C:Celsius (°C), F:Fahrenheit (°F)` | Unité d'affichage de la température. |
| `temp_offset` | `float` | `""` | `-30.0` à `30.0` | Offset de calibration pour compenser la dissipation thermique du microcontrôleur (laisser vide pour le réglage général). |
| `refresh_interval` | `Options` | `10` | `1`, `5`, `10`, `15`, `30`, `60` min | Fréquence de rafraîchissement des données météo et boursières. |
| `format_24h` | `Options` | `system` | `system:System (Général), 24h:24 Heures, 12h:12 Heures` | Format d'affichage de l'heure 24h ou 12h AM/PM. |
| `lang` | `Options` | `system` | `system:System (Général), fr:Français, en:English, es:Español` | Langue des descriptions météo et libellés des widgets (`system` synchronise avec la langue générale). |
| `show_markets` | `bool` | `true` | `true`, `false` | Affiche le bandeau ticker défilant des cryptomonnaies et actions. |
| `tracked_markets` | `String` (Multi) | `BTC,ETH,SOL,NVDA` | Tags Top 20 / Saisie libre | Cryptos via Binance (`BTC`, `ETH`, `SOL`, `DOGE`, `XRP`, `PEPE`, `KAS`, `TAO`, `SUI`...) et Actions/ETFs via Yahoo Finance (`NVDA`, `AAPL`, `TSLA`, `MSFT`, `GOOG`, `AMZN`, `SPY`, `QQQ`, `PLTR`, `MSTR`...). |
| `show_sysinfo` | `bool` | `true` | `true`, `false` | Affiche la jauge d'état système (RAM, CPU, WiFi). |
| `show_date` | `bool` | `true` | `true`, `false` | Affiche le badge jour et date. |
| `show_seconds` | `bool` | `true` | `true`, `false` | Affiche la trotteuse ou les chiffres des secondes. |
| `smooth_seconds` | `bool` | `true` | `true`, `false` | Balayage continu fluide de la trotteuse (vs tic-tac net 1s). |
| `offset_x` | `int` | `0` | `-64` à `64` | Décalage horizontal en pixels. |
| `offset_y` | `int` | `0` | `-32` à `32` | Décalage vertical en pixels. |

> **Auto-Scaling Dynamique** : Lorsque des widgets sont masqués, l'affichage se redimensionne dynamiquement pour occuper 100% de la matrice sans bande noire ni espace vide.

### Moteur : `audiovisualizer` (Visualiseur Audio Temps Réel)
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `style` | `Options` | `spectrum` | `spectrum`, `waveform`, `radial`, `neon_fire` | Mode de rendu : Barres de spectre FFT, Oscilloscope, Cercle Radial ou Flamme Néon. |
| `gain` | `int` | `24` | `0` à `30` | Gain matériel du microphone en dB (codec ES7210). |
| `color_theme` | `Options` | `rainbow` | `rainbow`, `neon`, `fire`, `matrix` | Dégradé de couleur des barres et ondes. |

### Moteur : `decibelMeter` (Sonomètre Décibel)
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `alert_threshold_db` | `int` | `85` | `40` à `120` | Seuil d'alerte visuelle en décibels. |
| `show_peak` | `bool` | `true` | `true`, `false` | Affiche l'indicateur de niveau crête (peak hold). |

### Moteur : `temp` (Moniteur Climat Intérieur)
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `show_humidity` | `bool` | `true` | `true`, `false` | Affiche le pourcentage d'humidité relative. |
| `temp_unit` | `Options` | `C` | `C`, `F` | Affichage Celsius ou Fahrenheit. |
| `temp_offset` | `float` | `-3.5` | `-30.0` à `30.0` | Offset de calibration thermique (dans l'unité configurée). |

### Moteur : `marquee`
| Champ | Type | Défaut | Description |
| :--- | :--- | :--- | :--- |
| *(auto)* | `None` | — | Moteur interne de synchronisation des marquees Pixelcade / Recalbox / Batocera reçus via MQTT ou Webhook. |

### Moteur : `mqttdata` (Données MQTT)
| Champ | Type | Défaut | Description |
| :--- | :--- | :--- | :--- |
| `topics` | `String` | `""` | Topics MQTT complets séparés par des virgules, une page chacun, dans cet ordre. Jusqu'à 6 (4 sur les cartes sans PSRAM). |

Affiche des pages publiées via MQTT par n'importe quel éditeur : une `value`, une `table` de 4 valeurs au plus, un `graph` ou une page `weather`, selon le `type` de chaque message. Seuls les topics se règlent ici ; tout le reste (durée par page, titres, couleurs) vient des messages, qui doivent être publiés **retenus**. L'écran ne se connecte que pendant qu'il est affiché et fixe lui-même sa durée (un cycle de ses pages) ; sa durée dans la rotation est ignorée. Protocole, formats, limites et messages : [MQTT_DATA_CONTRACT_FR.md](MQTT_DATA_CONTRACT_FR.md). Blueprints Home Assistant : [HOME_ASSISTANT_FR.md](HOME_ASSISTANT_FR.md).

Exemple d'instance : `{"instance_id":"home_data","engine_id":"mqttdata","config":{"topics":"arcadematrix/weather/local,arcadematrix/graph/inside,arcadematrix/value/outside"}}`.

### Moteurs : `retro_games`, `movie_showcase`, `series_showcase`, `anime_showcase` (DMD Media Showcase)
> [!IMPORTANT]
> Ces 4 moteurs Showcase **fonctionnent exclusivement avec l'application compagnon [dmd_media_showcase](https://github.com/red77290/dmd_media_showcase) installée sur une autre machine du même réseau local** (Raspberry Pi, NAS, serveur Docker ou PC). La connexion au serveur se configure une seule fois globalement dans le bloc racine `showcase` de `config.json` (ou via la carte unifiée **Showcase Server** dans l'onglet Settings de la Web UI).

| Champ Racine `showcase` | Type | Défaut | Description |
| :--- | :--- | :--- | :--- |
| `showcase.enabled` | `bool` | `false` | Interrupteur d'activation global (désactivé par défaut afin d'éviter tout accès réseau inutile si `dmd_media_showcase` n'est pas installé). |
| `showcase.server_url` | `String` | `"http://dmdmediashowcase.local:8890"` | URL HTTP pré-remplie (auto-découverte mDNS) de votre serveur **[dmd_media_showcase](https://github.com/red77290/dmd_media_showcase)** sur le réseau local (confirmée par le témoin **`🟢 Communication OK`** dans la Web UI). |
| `showcase.transport_mode` | `Options` | `mqtt_tcp` | Protocole de transport (`mqtt_tcp` pour contrôle MQTT temps réel + streaming HTTP/1.1 direct `DMDZ` v2 / `DMDS` sans perte zéro-heap, ou `http_poll` pour polling HTTP direct). |
| `showcase.use_showcase_broker` | `bool` | `true` | Se connecte automatiquement au broker MQTT hébergé sur la machine Showcase (`<server_ip>:1883`). |
| `showcase.mqtt_broker_source` | `Options` | `global` | Source du broker MQTT lorsque `use_showcase_broker` vaut `false` (`global` utilise `mqtt.server`, `custom` utilise `mqtt_custom_url`). |
| `showcase.mqtt_custom_url` | `String` | `""` | URL personnalisée du broker MQTT (ex. `mqtt://192.168.1.200:1883`) lorsque `mqtt_broker_source` vaut `custom`. |

| Champ d'Instance (`retro_games`, `movie_showcase`, `series_showcase`, `anime_showcase`) | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `text_scroll_speed` | `Options` | `4` | `1` à `10` (`slow`, `normal`, `fast`) | Vitesse de lecture du défilement intégral du synopsis sur la fiche info (`1` = Lent .. `10` = Turbo). |
| `jacket_panorama` | `bool` | `true` | `true`, `false` | Défilement vertical panoramique de la jaquette/affiche pleine largeur après la fiche info. |
| `marquee_panorama` | `bool` | `false` | `true`, `false` | Défilement vertical du marquee d'arcade ou de la bannière après l'affiche. |
| `snapshot_panorama_count` | `int` | `0` | `0` à `5` | Nombre de captures d'écran additionnelles à faire défiler verticalement (`0`..`5`). |
| `video_max_duration_s` | `int` | `8` | `2` à `120` | Durée maximale en secondes de l'extrait vidéo RGB565. |
| `video_fps` | `int` | `15` | `8` à `60` | Cadence d'images cible pour les extraits vidéo RGB565 diffusés. |
| `video_mode` | `Options` | `auto_action` | `auto_action`, `panorama`, `center_crop` | Stratégie de cadrage vidéo propulsée par `dmd_gif_converter` (`auto_action` IA Let Me Handle It, `panorama` balayage vertical haut-bas, ou `center_crop` recadrage centré fixe). |
| `show_poster` | `bool` | `true` | `true`, `false` | Affiche la mini-jaquette sur la fiche info. |
| `show_synopsis` | `bool` | `true` | `true`, `false` | Fait défiler le synopsis traduit complet sur la fiche info. |
| `show_video` | `bool` | `true` | `true`, `false` | Diffuse l'extrait vidéo RGB565 s'il est disponible. |
| `color_boost` | `Options` | `auto` | `auto`, `pixel_art`, `anime`, `cinema` | Profil de colorimétrie pour dalles LED HUB75 (`auto` IA Smart Color Boost Let Me Handle It, ou préréglage fixe `pixel_art`, `anime`, `cinema`). |
| `lang` | `Options` | `system` | `system,en,fr,es,de,it` | Langue des titres et synopsis (`system` se synchronise sur la langue configurée du DMD). |
| `systems` | `List` | `""` | Dynamique (`/api/v1/games/filters`) | (`retro_games` uniquement) Filtrer par systèmes arcade/consoles (laisser vide pour tous). |
| `genres` | `List` | `""` | Dynamique (`/api/v1/{domain}/filters`) | Filtrer par genres récupérés dynamiquement depuis le serveur Showcase. |
| `decades` | `List` | `""` | Dynamique (`/api/v1/{domain}/filters`) | Filtrer par décennies récupérées dynamiquement depuis le serveur Showcase. |
| `video_type` | `Options` | `any` / `opening` | `any`, `trailer`, `scene`, `opening`, `ending` | (`movie_showcase`, `series_showcase`, `anime_showcase`) Type d'extrait vidéo privilégié. |

---

## 10. Architecture de Stockage Modulaire & Cache Working-Set

ArcadeMatrix v4 découple la persistance de configuration du matériel de carte SD physique grâce à la couche d'abstraction `IConfigStorage` :

```text
 ┌─────────────────────────────────────────────────────────────┐
 │                      ConfigLoader                           │
 └──────────────┬───────────────────────────────┬──────────────┘
                │                               │
                ▼                               ▼
 ┌─────────────────────────────┐ ┌─────────────────────────────┐
 │      WorkingSetCache        │ │       IConfigStorage        │
 │                             │ │                             │
 │ • Suivi bits dirty          │ │ • SdConfigStorage (Hardware)│
 │ • Mutations atomiques RAM   │ │ • MemoryConfigStorage (Mock)│
 │ • Zéro blocage FS Core 1    │ │ • Sémantique rename atomique│
 └─────────────────────────────┘ └─────────────────────────────┘
```

1. **Interface `IConfigStorage`** : Backend de système de fichiers abstrait supportant l'écriture atomique de chaînes (`writeStringAtomic`), la lecture en flux, la vérification d'existence et le listage de répertoires.
2. **`SdConfigStorage`** : Backend matériel de production gérant SdFat avec verrouillage matériel SPI (`SdLockGuard`), écrivant dans un fichier temporaire (`.tmp`) suivi d'un renommage atomique pour éliminer la corruption lors de coupures d'alimentation impromptues.
3. **`MemoryConfigStorage`** : Backend en mémoire vive (Heap/RAM) utilisé pour les tests unitaires isolés hors-cible (`test_core`), la simulation et le fonctionnement sans carte SD.
4. **`WorkingSetCache`** : Gestionnaire d'état dirty en mémoire sur Core 0. Les mutations (sauvegardes Web UI ou mises à jour MQTT) mettent immédiatement à jour le cache en RAM et publient des instantanés atomiques vers Core 1 sans attendre les I/O lentes de la carte SD. Une synchronisation asynchrone écrit les fichiers sales sur le stockage permanent en arrière-plan.

---

*Note : L'ensemble des schémas de configuration peut également être interrogé en direct au format JSON via `GET /api/engines` (catalogue léger) ou de manière granulaire par moteur avec ses champs complets via `GET /api/engines/{id}`.*
