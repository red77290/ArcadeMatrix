# Aperçu de l'Architecture (ESP32)

🇬🇧 [English](ARCHITECTURE.md) | 🇫🇷 Français | 🇪🇸 [Español](ARCHITECTURE_ES.md)

Ce document présente une vue d'ensemble détaillée de l'architecture d'ArcadeMatrix pour le microcontrôleur ESP32.

---

## 1. Philosophie Centrale : Contraintes Matérielles

Contrairement à la version Raspberry Pi qui s'appuie sur un pipeline de rendu haut niveau en Python, la version ESP32 est développée en **C++** et conçue autour de contraintes matérielles strictes :
- **Limites RAM (320 Ko interne) :** Impossible d'instancier de lourds canvas dynamiques hors écran. Chaque octet compte.
- **Contraintes CPU (240 MHz) :** Pour maintenir 60 IPS sur la matrice, le rendu doit être extrêmement rapide.
- **Accès Direct DMA :** Les primitives de dessin sont écrites directement dans le buffer DMA matériel via la bibliothèque `ESP32 HUB75 LED MATRIX PANEL DMA Display` et `Adafruit GFX`.

---

## 2. Architecture Matérielle Modulaire Embarquée avec Rendu Direct

Plutôt qu'une couche d'abstraction lourde, l'ESP32 utilise une **Architecture Matérielle Modulaire Embarquée avec Rendu Direct**.

### Composants

1. **Moteurs Indépendants (`src/ClockEngine.cpp`, `src/DateEngine.cpp`, `src/CryptoEngine.cpp`, `src/StockEngine.cpp`, etc.)** : Chaque moteur est un sous-système autonome gérant son état et dessinant directement sur la matrice.
2. **Moteurs Crypto & Bourse** : Téléscripteurs de cours financiers en temps réel avec stratégie multi-fournisseurs (CoinGecko, Binance, Yahoo Finance) et cache TTL configurable.
3. **Moteur Fighter (Streaming Carte SD)** : Les sprites MUGEN sont lus directement depuis la carte SD au format binaire `.fgt` et dessinés en surimpression.

---

## 3. Couche d'Abstraction Matérielle & Coordination des Capteurs / Audio

Le `HardwareHAL` agit comme un conteneur centralisé gérant les bus périphériques et la détection matérielle :

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

RotationManager
      │
      ├── TEMP ──► nécessite le capteur SHTC3 (ignoré si absent)
      └── DECIBEL ─► nécessite l'échantillonnage audio (ignoré si absent)
```

### Coordination du Cycle de Vie
- **Température & Humidité I2C (SHTC3) :** `HardwareHAL` sonde le capteur au démarrage. Si absent, `isTempSensorAvailable()` retourne `false`, permettant à `RotationManager` de passer automatiquement le module `TEMP`.
- **Entrée Audio I2S (ES7210 / Micro) :** L'échantillonnage audio est partagé entre `DecibelEngine` et `VisualizerEngine`. La coordination de démarrage/arrêt est gérée par `HardwareHAL`.
- **Modèle du Visualiseur :** `VisualizerEngine` calcule un pseudo-spectre de bandes d'énergie d'amplitude optimisé pour le rendu matriciel LED à haute fréquence d'affichage.
- **Indicateur Décibel :** `DecibelEngine` calcule une valeur RMS convertie en un indicateur relatif de niveau sonore étalonnable.
