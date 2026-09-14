🇬🇧 [English](ARCHITECTURE.md) | 🇫🇷 Français | 🇪🇸 [Español](ARCHITECTURE_ES.md)

# Vue d'Ensemble de l'Architecture (ESP32 — C++ / FreeRTOS)

Ce document est la référence **exhaustive et approfondie** de l'architecture ArcadeMatrix sur ESP32 & ESP32-S3 (développé en **C++** avec **FreeRTOS**). Il détaille la philosophie de conception, le contrat `IEngine`, le registre d'auto-découverte `EngineRegistry` & `EngineRegistrar`, le cycle de vie "Lazy-Once", le pipeline de configuration auto-réparateur (`ConfigSanitizer`), l'interface WebUI dynamique pilotée par schéma, le `DisplayArbiter`, le compositeur d'overlay transverse (`OverlayManager` pour MUGEN Fighter), le modèle de threading double cœur, et les sous-systèmes autonomes Audio et Gyroscope.

> Si vous souhaitez **ajouter** un moteur ou un champ de configuration, lisez [DEVELOPER.md](DEVELOPER_FR.md). Ce document explique le **pourquoi** et le **comment** du système.

---

## Table des Matières

1. [Philosophie : Contraintes Embarquées & Zéro Churn Mémoire](#1-philosophie--contraintes-embarquées--zéro-churn-mémoire)
2. [Cartographie des Composants de Haut Niveau](#2-cartographie-des-composants-de-haut-niveau)
3. [Le Contrat Moteur (Modèle `IEngine`)](#3-le-contrat-moteur-modèle-iengine)
4. [Auto-Découverte : Registry, Registrar, Handlers & Gating](#4-auto-découverte--registry-registrar-handlers--gating)
5. [Cycle de Vie d'Instance "Lazy-Once"](#5-cycle-de-vie-dinstance-lazy-once)
6. [Modèle de Configuration : `config.json` → Instances](#6-modèle-de-configuration--configjson--instances)
7. [Auto-Réparation : Le `ConfigSanitizer`](#7-auto-réparation--le-configsanitizer)
8. [Propagation & Rechargement à Chaud sans Reboot](#8-propagation--rechargement-à-chaud-sans-reboot)
9. [WebUI Dynamique & Endpoints d'Options](#9-webui-dynamique--endpoints-doptions)
10. [Architecture d'Internationalisation (i18n) & Source Unique](#10-architecture-dinternationalisation-i18n--source-unique)
11. [Couche d'Abstraction Matérielle (`HardwareHAL`) & Gating](#11-couche-dabstraction-matérielle-hardwarehal--gating)
12. [L'Arbitre d'Affichage (`DisplayArbiter`)](#12-larbitre-daffichage-displayarbiter)
13. [Le Compositeur d'Overlay Transverse (`OverlayManager`)](#13-le-compositeur-doverlay-transverse-overlaymanager)
14. [Exécution Double-Cœur & Isolation FreeRTOS](#14-exécution-double-cœur--isolation-freertos)
15. [Régulation de Cadence & Double-Buffering DMA](#15-régulation-de-cadence--double-buffering-dma)
16. [Sous-Système Audio Autonome (`AudioHub` & `AudioOutputHAL`)](#16-sous-système-audio-autonome-audiohub--audiooutputhal)
17. [Orientation Gyroscopique (`GyroHAL` & `DisplayOrientationManager`)](#17-orientation-gyroscopique-gyrohal--displayorientationmanager)
18. [Surface API REST HTTP](#18-surface-api-rest-http)
19. [Métadonnées de Build & Télémétrie](#19-métadonnées-de-build--télémétrie)

---

## 1. Philosophie : Contraintes Embarquées & Zéro Churn Mémoire

L'ESP32 standard dispose d'environ 320 Ko de SRAM interne (et jusqu'à 8 Mo de PSRAM sur ESP32-S3). Le driver LED matrix HUB75 consomme une part importante de mémoire DMA et exige une cadence d'horloge très stable pour éviter tout scintillement.

Pour garantir un affichage fluide à 60 FPS sans fragmentation :
- **Allouer une seule fois, muter sur place :** Les buffers et tableaux d'animation sont créés dans `initialize()` et réutilisés à chaque frame.
- **Cycle de Vie "Lazy-Once" :** Un moteur n'est instancié que lorsque son instance configurée est affichée pour la première fois, puis conservé en mémoire pendant toute la durée d'exécution.
- **Isolation des Cœurs :** Le Cœur 1 est dédié au rendu graphique temps réel (`DisplayArbiter`, `IEngine::render()`, `OverlayManager`, DMA), tandis que le Cœur 0 gère le réseau, `AsyncWebServer`, mDNS, les décodeurs audio et les capteurs.
- **Les fonctionnalités transverses sont des Overlays, PAS des Engines :** MUGEN Fighter vit dans `OverlayManager`, préservant la pureté de `EngineRegistry`.

---

## 2. Cartographie des Composants de Haut Niveau

```mermaid
flowchart TD
    subgraph Boot["Démarrage (Cœur 1)"]
        MAIN["main.cpp (setup)"] --> HAL["HardwareHAL.begin() (Sondes I2C & I2S)"]
        HAL --> CFG["ConfigLoader::load() + ConfigSanitizer::sanitize()"]
        CFG --> REG["EngineRegistrar::registerAll()"]
        REG --> RM["RotationManager::begin()"]
        RM --> ARB["DisplayArbiter"]
    end

    subgraph Core0["Cœur 0 : Réseau & Services"]
        WS["AsyncWebServer (Port 80)"]
        WS --> API["API REST (/api/v1/*, /api/engines, /api/instances)"]
        API --> SAN["ConfigSanitizer"]
        SAN --> SAVE["config.json (Sauvegarde Atomique)"]
        MDNS["Répondeur mDNS"]
        AH["AudioHub (Arbitre Audio Arrière-Plan)"]
        AH --> AHAL["AudioOutputHAL (DAC I2S TX)"]
    end

    subgraph Core1["Cœur 1 : Boucle de Rendu (FreeRTOS)"]
        LOOP["main.cpp (loop)"] --> ARB_EVAL["DisplayArbiter::evaluate()"]
        ARB_EVAL --> RM_LOOP["RotationManager::loop() (Lazy-Once)"]
        RM_LOOP --> ENG["IEngine Actif (update + render)"]
        ENG --> MATRIX["MatrixPanel_I2S_DMA (Framebuffer)"]
        RM_LOOP --> OV["OverlayManager::render() (Passe Fighter)"]
        OV --> MATRIX
        MATRIX --> DMA["DMA Flip Buffer vers LEDs HUB75"]
    end

    API -.->|"actionMutex queue (RECREATE_INSTANCE / NOTIFY_CONFIG)"| RM
    SAVE -.->|"ConfigLoader rechargement à chaud"| RM_LOOP
```

---

## 3. Le Contrat Moteur (Modèle `IEngine`)

Chaque moteur implémente l'interface `IEngine` (`include/core/EngineContract.h`) :

```mermaid
classDiagram
    class IEngine {
        <<interface>>
        +initialize(context, config) EngineError*
        +activate()*
        +update(context)*
        +render(context)*
        +deactivate()*
        +onConfigChanged(config)
        +isFinished() bool
        +isRealtime() bool
        +setRotationBudget(budget)
        +selfPaced() bool
    }

    class EngineDescriptor {
        +EngineMetadata metadata
        +EngineCapabilities capabilities
        +EngineRequirements requirements
        +ConfigSchema schema
        +EngineFactory factory
    }

    class EngineMetadata {
        +String id
        +String name
        +String category
        +String version
    }

    class EngineCapabilities {
        +bool supports_128x32
        +bool supports_256x64
        +bool realtime
        +bool interruptible
        +bool selfPaced
        +bool allowsOverlay
    }

    class EngineRequirements {
        +bool needsPsram
        +bool needsAudio
        +bool needsTempSensor
        +bool needsGyroscope
        +bool needsNetwork
        +bool needsSd
    }

    class ConfigSchema {
        +vector~ConfigField~ fields
    }

    class ConfigField {
        +String id
        +ConfigType type
        +String label
        +String description
        +String default_value
        +bool required
        +String min_val
        +String max_val
        +String step
        +String unit
        +String options_endpoint
        +bool is_multiselect
        +String visible_when
        +ValidationPolicy validation_policy
    }

    EngineDescriptor *-- EngineMetadata
    EngineDescriptor *-- EngineCapabilities
    EngineDescriptor *-- EngineRequirements
    EngineDescriptor *-- ConfigSchema
    ConfigSchema *-- ConfigField
```

---

## 4. Auto-Découverte : Registry, Registrar, Handlers & Gating

1. Chaque moteur encapsule ses métadonnées, son schéma `ConfigSchema`, ses prérequis matériels `EngineRequirements` et sa factory dans un `IEngineDescriptorHandler`.
2. Au démarrage, `EngineRegistrar::registerAll()` compare les exigences avec `hardwareHAL.capabilities()`.
3. Seuls les moteurs supportés sont activés dans `EngineRegistry`. Les moteurs non compatibles sont enregistrés avec `available: false` et un message explicatif pour la WebUI.

---

## 5. Cycle de Vie d'Instance "Lazy-Once"

- **Instanciation Paresseuse :** Créé uniquement au premier affichage.
- **Cache Permanent :** L'instance reste en mémoire dans `activeEngines[instance_id]`.
- **Transitions Propres :** Appel de `deactivate()` puis `activate()` lors des rotations de carrousel.

---

## 6. Modèle de Configuration : `config.json` → Instances

```json
{
  "system": { "brightness": 128, "lang": "fr" },
  "display": { "auto_rotate": true, "manual_rotation": 0 },
  "audio": { "master_volume": 80, "enable_bluetooth": true, "enable_webradio": true },
  "rotation": [
    { "instance_id": "clock_main", "duration": 15, "overlays": { "fighter": true } },
    { "instance_id": "weather_paris", "duration": 10 },
    { "instance_id": "music_main", "duration": 20, "overlays": { "fighter": true } }
  ],
  "instances": [
    { "id": "clock_main", "engine_id": "clock", "config": { "theme": "street_fighter" } },
    { "id": "weather_paris", "engine_id": "weather", "config": { "city": "Paris" } },
    { "id": "music_main", "engine_id": "music_player", "config": { "show_progress": true } }
  ]
}
```

---

## 7. Auto-Réparation : Le `ConfigSanitizer`

Valide la configuration à chaque sauvegarde ou démarrage :
- Injection des valeurs par défaut manquantes.
- Bornage automatique (`clamp`) des valeurs numériques.
- Suppression des entrées de rotation orphelines.

---

## 8. Propagation & Rechargement à Chaud sans Reboot

Les modifications de configuration appliquées via l'API sont injectées en mémoire sans redémarrage :
- `NOTIFY_CONFIG_CHANGED` : L'instance reçoit `onConfigChanged()` pour relire ses valeurs sur place.
- `RECREATE_INSTANCE` : L'instance est recréée proprement si des buffers majeurs changent.

---

## 9. WebUI Dynamique & Endpoints d'Options

L'interface WebUI ne contient **aucun formulaire codé en dur**. Elle lit `GET /api/engines` pour générer automatiquement les champs de formulaire et interroge des endpoints dynamiques (`options_endpoint`, ex: `/api/clocks/themes`) pour garnir les listes déroulantes.

---

## 10. Architecture d'Internationalisation (i18n) & Source Unique

Prise en charge native de l'anglais, du français et de l'espagnol :
- Dictionnaires centralisés dans `src/core/I18n.cpp`.
- Schémas canoniques en anglais avec clés de traduction automatiques dans la WebUI selon `config.system.lang`.

---

## 11. Couche d'Abstraction Matérielle (`HardwareHAL`) & Gating

- **Câblage 100 % Gelé :** Les broches définies dans `HardwareProfile.h` sont **strictement immuables**.
- **Instantané des Capacités (`AudioCapabilities`) :**
  ```cpp
  struct AudioCapabilities {
      bool input = false;          // Microphone I2S
      bool output = false;         // DAC I2S
      bool fullDuplex = false;      // Support RX + TX simultanés
      uint32_t maxSampleRate = 44100;
      uint8_t maxChannels = 2;
      bool bluetoothClassic = false;
      bool psram = false;
  };
  ```

---

## 12. L'Arbitre d'Affichage & Runtime d'Affichage (`DisplayArbiter`, `DisplayRuntime`)

ArcadeMatrix sépare strictement la décision d'arbitrage de l'exécution du cycle de vie des moteurs :

```text
[ Alertes d'Urgence / OTA ] (Priorité 100, ONE_SHOT / UNTIL_CANCELLED)
             ↓
[ Interruptions Temps Réel : Marquee MQTT / Alertes Live ] (Priorité 75)
             ↓
[ Visualiseur Audio / Requête Moteur Actif ] (Priorité 60)
             ↓
[ Carrousel de Rotation Actif : Horloge, Météo, Musique ] (Priorité 50)
             ↓
[ Écran de Repli : Horloge Digitale par Défaut ] (Priorité 10)
```

### Architecture SPSC Lock-Free & Déterministe Zéro-Allocation
- **File de Commandes SPSC (Single Producer, Single Consumer) :** Le Cœur 0 (serveur Web, écouteur MQTT, AudioHub) émet ses requêtes de manière asynchrone via `m_displayArbiter.submitRequest(request)` et `cancelRequest(sourceId)`. Ces commandes sont poussées dans un ring-buffer lock-free (`LockFreeSPSCQueue<ArbiterCommand, 16>`).
- **Propriétaire Unique sur Cœur 1 :** Le Cœur 1 est le **seul propriétaire** du tableau statique de slots (`std::array<DisplayRequestSlot, 8>`). Au début de `DisplayArbiter::evaluate()`, le Cœur 1 dépile les commandes en attente et évalue les priorités en $O(1)$ avec **ZÉRO mutex** et **ZÉRO allocation dynamique**.
- **Contrat Décisionnel Pur :** `DisplayArbiter::evaluate()` retourne une structure `DisplayDecision` légère contenant uniquement des identifiants sémantiques (`sourceId`, `engineHandle`, `priority`, `requestId`, `needsClear`, `allowsOverlay`, `isRealtime`), sans aucun pointeur brut `IEngine*`.
- **Identité Canonique `EngineHandle` :** Les instances sont identifiées par une structure POD `EngineHandle` (`descriptorId[32]`, `instanceId[32]`) sans allocation de `String`. `DisplayRuntime::resolveEngine()` résout les moteurs de façon canonique sans heuristique (`sourceId / 10`).
- **Auto-Consommation `ONE_SHOT` :** Les alertes non récurrentes sont consommées de manière atomique lors de leur évaluation.
- **Préservation des Request IDs :** Le rafraîchissement d'une requête préserve son `requestId` unique sauf demande explicite de réinitialisation de timer.

### Centralisation du Cycle de Vie & Préemption (`DisplayRuntime`)
- **Propriétaire Unique du Cycle de Vie :** `DisplayRuntime` est le **seul propriétaire** des transitions de cycle de vie (`activate()`, `deactivate()`, `pause()`, `resume()`).
- **Sémantique Préemption vs Transition :**
  - **Préemption Temporaire (ex. Alerte MQTT sur Horloge) :** Le moteur sortant reçoit `pause()`, l'alerte entrante reçoit `activate()`.
  - **Fin de Préemption (Retour au carrousel) :** L'alerte terminée reçoit `deactivate()`, l'horloge en pause reçoit `resume()`, préservant intacts son état interne et son animation.
  - **Transition de Carrousel (ex. Horloge → Météo) :** Le moteur sortant reçoit `deactivate()`, le moteur entrant reçoit `activate()`.
- **Préemption & Composition d'Overlays :** Si la décision autorise les overlays (`decision.allowsOverlay == true`), `OverlayManager` superpose les effets transverses (combattants MUGEN) par-dessus l'affichage.

---

## 13. Configuration Lock-Free SRSW Prouvée Formellement (`ConfigSnapshot`)

Pour éliminer les courses entre cœurs et éviter tout mutex sur la boucle de rendu critique du Cœur 1 :
- **Machine à 4 États Atomiques (`SlotState`) :** `ConfigLoader` gère 3 buffers physiques de snapshots via des états atomiques explicites : `FREE`, `WRITING`, `PUBLISHED` et `READING`.
- **Réservation Linéarisable par Boucle CAS (Cœur 1) :** `ConfigSnapshotGuard guard = config.acquireSnapshot();` exécute une boucle CAS atomique : `_slotStates[slot].compare_exchange_weak(PUBLISHED, READING)` avec sémantique acquire. L'accès au payload `_snapshots[slot]` est **strictement impossible avant le succès du CAS**. Le guard RAII (move-only) gère la restitution automatique de l'état du slot à sa destruction.
- **Recyclage Sécurisé & Publication Différée (Cœur 0) :** Le writer réserve dynamiquement un slot `FREE` ou recycle un ancien `PUBLISHED` via `compare_exchange_strong(PUBLISHED, FREE)`. Si les 3 slots sont occupés (`READING + PUBLISHED + WRITING`), `_publishPending = true;` est positionné et le writer **retourne immédiatement sans attente active ni `yield()`**, garantissant la publication consolidée de la version la plus récente dès qu'un slot redevient `FREE`.
- **Linéarité & Intégrité Checksum :** Version monotone avec double-magic et intégrité checksum garantissant une cohérence absolue entre threads.
- **Mutations Transactionnelles :** Toutes les modifications provenant du Cœur 0 transitent par `config.mutate([&](ConfigLoader& cfg) { ... })`.

---

## 14. Le Compositeur d'Overlay Transverse (`OverlayManager`)

- Rendus superposés après la passe graphique du moteur d'arrière-plan.
- Décodage des sprites animés `.fgt.gz` pour les combattants MUGEN.
- Activation par rotation dans `config.rotation[i].overlays.fighter`.
- **Fighter est un overlay transverse, PAS un engine dans `EngineRegistry`.** En cas de préemption par une alerte prioritaire, `DisplayRuntime` suspend l'overlay en douceur sans détruire les assets d'arrière-plan.

---

## 15. Exécution Double-Cœur & Isolation FreeRTOS

- **Cœur 0 (Services & Réseau) :**
  - `AsyncWebServer` (requêtes HTTP et mutations de configuration).
  - Sessions audio (`AudioSessionManager`, `WebRadioService`, `BluetoothAudioService`).
  - Analyse audio FFT et sondes de capteurs.
- **Cœur 1 (Graphisme Temps Réel) :**
  - Évaluation de l'arbitre (`DisplayRuntime::update()`).
  - Régulation de cadence via `FrameScheduler` (60 FPS pour les moteurs temps réel, 20-30 FPS pour les écrans statiques).
  - Rendu moteur actif (`update()` / `render()`), passe d'overlay (`OverlayManager`) et DMA flip buffer.

---

## 16. Sous-Système Audio Autonome (`AudioHub` & `AudioOutputHAL`)

```text
Services Audio (BT, Spotify, AirPlay, WebRadio)
    ↓ (PCM + Métadonnées)
AudioHub (État, Génération & Arbitrage)
    ├──► AudioOutputHAL (Hardware DAC I2S TX)
    ├──► AudioAnalysisService (Spectre FFT / RMS)
    └──► ArtworkService (Cache Image PSRAM)
            ↓
      AudioPlaybackState
            ↓
       MusicEngine (Présentation Visuelle Uniquement)
```

- **`AudioHub`** arbitre les sources et met à jour un `AudioPlaybackState` avec identifiant `generation`.
- **`AudioOutputHAL`** est l'unique abstraction autorisée à parler au DAC physique.
- **`MusicEngine`** affiche l'état sans jamais toucher au matériel audio ni aux sockets réseau.

---

### Gestion Avancée de la Mémoire (ESP32 vs ESP32-S3 avec PSRAM)

| Carte Matérielle | SRAM Interne | PSRAM Externe | Mémoire DMA | Stratégie SSL / TLS | Résolutions Max |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **ESP32 Classic (`esp32dev`)** | ~320 Ko (partagée FreeRTOS / Wi-Fi) | Aucune | SRAM Interne (compatible DMA) | **Désactivé :** Les buffers TLS (~45-60 Ko) privent le DMA et causent des crashs. Moteurs SSL lourds (Crypto, Bourse) désactivés via `EngineCapabilities`. | `128x32` / `64x32` |
| **ESP32-S3 Waveshare (`esp32s3_waveshare`)** | ~320 Ko (SRAM centrale) | **8 Mo / 16 Mo Octal PSRAM** | SRAM Interne uniquement (`MALLOC_CAP_INTERNAL \| MALLOC_CAP_DMA`) | **100% SRAM interne, sérialisé :** mbedTLS utilise l'allocateur standard ESP-IDF/Arduino (jamais la PSRAM — voir ci-dessous). Toutes les poignées de main TLS du système sont sérialisées via `NetworkBudget::ScopedTlsHandshakeLock`. | `256x64` / `64x256` |

#### Résolution de l'Épuisement Mémoire SSL / TLS (et pourquoi mbedTLS n'utilise plus jamais la PSRAM)
Sur microcontrôleur, une connexion HTTPS exige d'importants buffers d'échange cryptographique (16 Ko in/out + ASN.1 + état de session ≈ 45 Ko par socket). Sur l'ESP32 classique, la coexistence des buffers DMA HUB75 et de connexions TLS causait une fragmentation sévère de la heap.

Une itération précédente de ce code tentait de résoudre ce problème en routant les allocations dynamiques de mbedTLS vers la PSRAM via un hook `mbedtls_platform_set_calloc_free()` personnalisé (`MbedTlsAllocator`), dans l'idée que la SRAM interne serait ainsi entièrement réservée au réseau temps réel et au DMA d'affichage. **Les tests sur matériel réel ont prouvé que cela corrompt activement l'affichage** : sur la carte ESP32-S3 Waveshare, le framebuffer HUB75 lui-même réside en PSRAM (`-D SPIRAM_DMA_BUFFER`, nécessaire car le déplacer en SRAM interne coûterait ~64 Ko que cette carte ne peut pas se permettre). Les buffers d'enregistrement TLS de mbedTLS (~32 Ko combinés) constituent exactement le type d'accès PSRAM volumineux et en rafale qui entre en contention avec les lectures PSRAM continues du moteur GDMA HUB75 pour le framebuffer, via le cache PSRAM partagé — toute poignée de main TLS avec des buffers résidant en PSRAM a systématiquement éteint l'affichage en quelques secondes. **Cet allocateur a été annulé et supprimé** (`MbedTlsAllocator.cpp/.h` supprimés) ; mbedTLS utilise de nouveau l'allocateur standard 100% SRAM interne sur les deux cibles, comme en `v3.1.0`.

L'intégrité de l'affichage prime strictement sur la fiabilité TLS : un échec TLS dû à la pression sur la SRAM interne dégrade proprement (les valeurs en cache sont conservées — voir `DashboardDataProvider`, `YahooFinanceProvider`, `BinanceProvider`), alors qu'un affichage corrompu ne peut se rétablir sans redémarrage. La SRAM interne étant désormais l'unique foyer du TLS et du DMA/réseau, la vraie solution est un **contrôle d'admission + sérialisation**, pas un routage d'allocateur :
1. **Filtrage via capacités sur ESP32 Classic :** Les moteurs réseau lourds (`CryptoEngine`, `StockEngine`) déclarent `EngineRequirements::needsPsram = true`. Sur les cartes sans PSRAM, `ConfigSanitizer` les désactive automatiquement sans crash.
2. **Sérialisation TLS Système (`NetworkBudget::ScopedTlsHandshakeLock`) :** Chaque point d'appel TLS du code (météo/marchés du Dashboard, Crypto, Bourse, Spotify, Google Cast, Artwork, repli HTTPS Marquee, GNews, synchronisation Pixelcade) construit un `ScopedTlsHandshakeLock` immédiatement avant `WiFiClientSecure::connect()`. Il s'agit d'un unique mutex FreeRTOS global : une seule poignée de main TLS peut être en cours n'importe où dans le firmware à un instant donné, bornant la demande de pointe en SRAM interne à une seule réservation d'environ 32 Ko au lieu d'un chevauchement à N voies. **Correction d'atomicité (ce tour) :** le constructeur revalide `NetworkBudget::canStartTlsSession()` *alors qu'il détient déjà le mutex*, juste avant de signaler le succès — fermant une fenêtre de compétition TOCTOU où le budget aurait pu être vérifié suffisant puis invalidé par une autre poignée de main/allocation pendant que la tâche attendait (jusqu'à 5s) le mutex contesté. Une pré-vérification bon marché et non-autoritative de `canStartTlsSession()` reste autorisée aux points d'appel, uniquement pour éviter de bloquer sur un budget déjà connu insuffisant ; seule la revérification interne du verrou, après acquisition, fait autorité.
3. **Limite connue non résolue :** cette sérialisation évite les crashs/corruptions d'affichage, mais ne résout **pas** la fragmentation sous-jacente de la SRAM interne. Sur matériel réel, avec `Dashboard` actif, la SRAM interne libre a été observée chutant de ~65 Ko à un plateau fragmenté d'environ 11-12 Ko (plus gros bloc ~3 Ko) en environ 3 minutes d'exécution. Comme `canStartTlsSession()` exige `largestInternalBlock >= 16896 octets`, **une fois ce plateau atteint, `GoogleCastEngine` (et tout autre consommateur TLS) peut se voir refuser l'admission indéfiniment** — cela se manifeste actuellement par **Google Cast qui ne se connecte/n'affiche jamais rien** une fois le système monté depuis quelques minutes avec Dashboard ou d'autres moteurs actifs, même si la découverte mDNS de l'appareil Cast réussit. C'est une dégradation acceptée (pas de crash, pas de corruption) mais **pas encore résolue** — voir la note Fragmentation / Travaux Futurs ci-dessous.

#### Concurrence : Audio Simultané & Rendu 60 FPS
- **Core 0 :** Décodage MP3 (`minimp3`), gestion des flux audio, Wi-Fi et requêtes réseau.
- **Core 1 :** Rendu matriciel à 60 FPS ininterrompu et affichage d'overlays.
- **Communication sans verrou :** `AudioHub` publie des snapshots atomiques `AudioPlaybackState` avec identifiant `generation` incrémental, lus instantanément par le Core 1 sans blocage ni mutex.

#### Problèmes Connus Non Résolus (à ce stade, pas encore corrigés)
- **Google Cast n'affiche actuellement rien / ne se connecte jamais** une fois le système monté depuis quelques minutes avec d'autres moteurs (notamment `Dashboard`) actifs : la découverte mDNS réussit, mais la poignée de main TLS est systématiquement refusée par `canStartTlsSession()` car le système se stabilise sur un plateau de SRAM interne fragmenté (~11-12 Ko libres, ~3 Ko de plus gros bloc), sous le seuil d'admission de 16896 octets. Aucun crash ni corruption d'affichage ne se produit — c'est le comportement de sécurité voulu de la porte d'admission — mais la fragmentation sous-jacente n'est pas résolue, seules ses conséquences sont contenues.
- **`sdmmc_read_blocks failed (257)`** survient encore par intermittence sous forte fragmentation (observé en parallèle du point ci-dessus) ; `ConfigLoader` et `GifEngine` se rétablissent proprement (restauration de sauvegarde / saut), mais la cause racine de pression d'allocation est partagée avec le problème d'admission TLS ci-dessus et nécessite une stratégie dédiée de défragmentation ou de réservation (par ex. un petit pool de SRAM interne réservé au démarrage pour les transactions compatibles DMA) plutôt qu'un contrôle d'admission au mieux.
- Une session précédente sur matériel réel a observé un dépassement de délai du Task Watchdog / `abort()` / redémarrage à environ 5,5 minutes de fonctionnement pendant que `GifEngine` était actif avec une heap sévèrement fragmentée et des tentatives de reconnexion TLS Cast en cours en parallèle. Le test d'endurance le plus récent (après la correction d'atomicité de l'admission TLS) a tourné environ 6 minutes sans que ce crash ne se reproduise, mais le test a été interrompu par un débranchement USB physique intentionnel avant qu'une fenêtre plus longue ne puisse confirmer la durabilité de la correction — **ce crash n'est pas encore confirmé comme corrigé**, seulement non reproduit sous la nouvelle sérialisation.

---

## 20. Architecture Multi-Résolutions & Géométrie Déclarative

ArcadeMatrix prend en charge toutes les résolutions et orientations (`64x32`, `128x32`, `256x64`, `128x64`, `64x64`, `32x64`, `32x128`, `64x128`, `64x256`).

### La Règle d'Or du Rendu Responsif
> **Les renderers ne contiennent aucun embranchement `if (layoutClass)`.**
> La classification est effectuée **une seule fois** par une calculatrice pure `*LayoutCalculator` produisant des structures déclaratives de `Rect`s bornés. Le moteur de rendu dessine exclusivement dans ces rectangles.

```text
                 DisplayGeometry (width, height, rotation, layoutClass, version)
                                       │
                                LayoutHelper (Stateless)
                                       │
                    ┌──────────────────┴──────────────────┐
                    ▼                                     ▼
           *LayoutCalculator                     *GeometryAdapter
           (ex: MusicLayout)                     (ex: FighterGeometry)
                    │                                     │
                    ▼                                     ▼
             Layout / Rects                        Geometry (groundY, spawns)
                    │                                     │
                    └──────────────────┬──────────────────┘
                                       ▼
                             Renderer Pur Unique
```

### Séquencement Multi-Core Strict à l'Apex
1. La rotation matérielle `display->setRotation(newRot)` est appliquée **exclusivement sur le Core 1 à l'apex de la transition**.
2. `DisplayGeometry` est actualisée directement depuis les dimensions actives `display->width()` / `display->height()`.
3. `onDisplayGeometryChanged(geometry)` est notifié à l'engine actif et à l'`OverlayManager`.
4. Les moteurs avec caches géométriques (`MatrixRainClock`, `TetrisClock`, `VisualizerEngine`, `FighterEngine`) reconfigurent leurs structures dérivées sans réinitialiser la logique métier ni la partie en cours.

### Bibliothèque Dual-GIF (YOKO & TATE)
- `/gifs/` : Animations optimisées pour le mode paysage (YOKO).
- `/gifs_tate/` : Animations optimisées pour le mode portrait (TATE).
- `GifSourceSelector` résout dynamiquement le dossier primaire et le dossier de repli sans dépendance de layout dans `GifEngine`.

---

## 18. Surface API REST HTTP

| Méthode | Route | Description |
| :-- | :-- | :-- |
| `GET` | `/api/v1/system/status` | Heap, PSRAM, uptime, Wi-Fi, capacités. |
| `GET` | `/api/engines` | Liste des descripteurs de moteurs et schémas. |
| `GET` | `/api/instances` | Liste des instances configurées. |
| `POST`| `/api/instances` | Création ou modification d'une instance. |
| `GET` | `/api/rotation` | Liste de lecture de la rotation. |
| `POST`| `/api/rotation` | Mise à jour de la séquence de rotation. |
| `GET` | `/api/audio/status` | État de lecture audio, source, volume. |
| `POST`| `/api/audio/volume` | Réglage du volume audio principal (0-100%). |
| `GET` | `/api/gyro/status` | Vecteur gravité, rotation active et effets de transition. |
| `POST`| `/api/gyro/calibrate` | Calibration du point zéro de référence ($0^\circ$ Normal). |
| `POST`| `/api/display/orientation` | Forçage de rotation, offset de montage et effet de transition. |
| `POST`| `/api/display/test-transition` | Déclenche un test visuel de l'effet de transition. |

---

## 19. Métadonnées de Build & Télémétrie

L'endpoint `/api/v1/system/version` expose l'empreinte exacte du build (`git_commit`, `build_timestamp`, `firmware_version`).
