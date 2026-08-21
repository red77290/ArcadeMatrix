🇬🇧 [English](ARCHITECTURE.md) | 🇫🇷 Français | 🇪🇸 [Español](ARCHITECTURE_ES.md)

# Aperçu de l'Architecture (ESP32 - C++)

Ce document présente une vue d'ensemble détaillée de l'architecture d'ArcadeMatrix pour le microcontrôleur ESP32. Il met en lumière la philosophie de développement C++ pour systèmes embarqués, la gestion critique de la RAM, et l'implémentation du cycle de vie "Lazy-Once".

---

## 1. Philosophie Centrale : Contraintes Matérielles

Contrairement à la version Raspberry Pi, la version ESP32 est développée en **C++** et conçue autour de contraintes matérielles sévères :
- **Limites de RAM et PSRAM :** L'architecture du noyau est optimisée pour fonctionner sur le plus petit dénominateur commun (un ESP32 classique avec ~320 Ko de RAM libre). Cependant, ArcadeMatrix supporte pleinement des cartes avancées comme l'**ESP32-S3** avec de la PSRAM (jusqu'à 16 Mo). *Attention :* certains moteurs très gourmands en mémoire (comme `CryptoEngine` et `StockEngine` qui stockent d'importants historiques de graphiques et analysent de gros payloads JSON) **exigent obligatoirement de la PSRAM** pour fonctionner. Les moteurs qui s'accommodent des 320 Ko l'utilisent, tandis que ceux nécessitant la PSRAM échoueront s'ils sont activés sur un ESP32 standard. La fragmentation de la Heap reste notre plus grand ennemi, d'où l'importance du cycle de vie contrôlé.
- **Contraintes CPU (240 MHz) :** Pour maintenir 60 IPS sur la matrice, le rendu doit être extrêmement rapide.
- **Accès Direct DMA :** Les primitives de dessin sont écrites directement dans le buffer matériel DMA I2S sans système d'exploitation intermédiaire.

Pour répondre à ces exigences et préserver la mémoire (Heap) de la fragmentation sur de longues durées de fonctionnement (uptime de plusieurs mois), ArcadeMatrix utilise un cycle de vie strict.

---

## 2. Le Cycle de Vie "Lazy-Once" et le Registre

L'architecture est structurée autour d'un **Engine Registry** qui découple le cœur du programme des implémentations individuelles de chaque moteur.

```mermaid
graph TD
                 Registry[Engine Registry]
                       │
                 Descriptor[EngineDescriptor]
                       │
                    Factory[Factory]
                       │
                 Instance[EngineInstance]
                       │
              ┌────────┴────────┐
              │                 │
        Context[ApplicationContext] Config[DictionaryEngineConfig]
              │                 │
              └────────┬────────┘
                       │
                 Runtime[Rotation Manager]
                       │
          ┌────────────┼────────────┐
          │            │            │
       activate      update       render
          │            │            │
          └────────────┼────────────┘
                       │
                  deactivate
```

### 2.1 Pourquoi un Registry C++ ?
L'ESP32 possède une taille de firmware limitée (Flash Memory). Le `EngineRegistry` utilise un patron de conception *Factory*. Il n'instancie **pas** les objets `Engine` au démarrage. Il conserve seulement des pointeurs vers des fonctions constructeurs (`EngineDescriptor`). Cela permet de gagner des dizaines de kilo-octets de RAM au boot : seuls les moteurs explicitement activés par l'utilisateur (via son `config.json`) seront un jour instanciés.

### 2.2 Explication des phases du cycle de vie :

1. **`initialize()` (Allocation Contrôlée) :** 
   * Appelée *une seule fois* lors de la découverte du moteur par le `RotationManager`. C'est l'unique moment où le moteur a le droit de réclamer de la mémoire Heap (chargement d'images depuis la SD, réservation de vecteurs `std::vector`).
   * *Pourquoi ne pas détruire et recréer les moteurs à chaque rotation pour libérer 100% de la RAM ?* Car sur un ESP32 sans MMU (Memory Management Unit) avancée, les allocations/désallocations répétées créent une **fragmentation mortelle de la Heap**. En gardant le moteur en vie (Lazy-Once), on stabilise la mémoire pour une durée de vie infinie du firmware.
2. **`activate()` (Préparation) :** 
   * Appelée lorsque le moteur prend le contrôle de l'écran.
3. **`update()` & `render()` (Hot Loop - 60 FPS) :**
   * **Contrainte absolue : Aucune allocation dynamique (`new`, `malloc`, `String()`).** Tout est calculé sur la pile (Stack) ou via des tampons alloués en phase 1, puis directement envoyé au contrôleur DMA matériel.
4. **`deactivate()` (Mise en veille) :**
   * Déconnexion des API externes, arrêt des timers.
5. **`is_finished()` :**
   * Permet de signaler au `RotationManager` que l'itération logique est terminée (ex: tous les sprites de combat sont passés).

---

## 3. Couche d'Abstraction Matérielle & Coordination des Capteurs / Audio

Le `HardwareHAL` agit comme un conteneur centralisé gérant les bus périphériques et la détection matérielle en "plug and play" :

```text
                    ┌───────────────────┐
                    │   HardwareHAL     │
                    └─────────┬─────────┘
                              │
             ┌────────────────┼────────────────┐
             │                │                │
             ▼                ▼                ▼
          HUB75             I2C             I2S
          Matrice           SHTC3           ES7210
             │                │                │
             ▼                ▼                ▼
          Rendu           TempEngine     Échantillonnage Audio
                                               │
                                    ┌──────────┴─────────┐
                                    ▼                    ▼
                              DecibelEngine       VisualizerEngine
```

### Coordination matérielle autonome
- **Capteurs (I2C) :** `HardwareHAL` sonde le capteur I2C au démarrage. Si `isTempSensorAvailable()` retourne `false`, le `RotationManager` ignorera automatiquement le moteur de température, évitant des crashs C++.
- **Microphone (I2S) :** Partagé entre le décibelmètre et le visualiseur audio (FFT). `HardwareHAL` gère intelligemment la fermeture du bus pour éviter les conflits d'échantillonnage de la RAM.

---

## 4. Multiprocessing et Asynchronisme (Dual-Core)

Le framework Arduino-ESP32 exploite les deux cœurs de l'ESP32 :
- **Core 0 (PRO_CPU) :** Gère de manière asynchrone la pile TCP/IP, le Wi-Fi, le serveur HTTP (`ESPAsyncWebServer`) et la réception des requêtes MQTT. Les configurations modifiées en vol (via `onConfigChanged`) sont injectées sans bloquer l'affichage.
- **Core 1 (APP_CPU) :** Exécute le `RotationManager`, gère l'orchestration des moteurs et écrit dans le buffer DMA via la bibliothèque d'affichage.

Cette stricte séparation permet à l'interface web de fonctionner à haut débit sans jamais faire saccader l'animation à l'écran.
