🇬🇧 [English](DEVELOPER.md) | 🇫🇷 Français | 🇪🇸 [Español](DEVELOPER_ES.md)

# Guide développeur (ESP32 - C++)

Bienvenue dans le guide de développement d'ArcadeMatrix pour ESP32. Ce document explique la marche à suivre pour étendre l'architecture et créer de nouveaux moteurs (Engines) en C++.

---

## 1. Comprendre l'Architecture : Engines, Registry et Lifecycle

ArcadeMatrix ne possède plus de liste codée en dur de ses fonctionnalités. Le système repose sur un **Registry** qui découvre les moteurs au démarrage.

### 1.1 Le Cycle de Vie Strict (Lazy-Once)

L'ESP32 possède une Heap extrêmement réduite (env. 320 Ko). Pour éviter les crashs (Kernel Panics) dus à la fragmentation de la mémoire, ArcadeMatrix impose un cycle de vie strict pour chaque `IEngine`.

```text
initialize()
    │
    ├── allocation via 'new' ou 'std::vector'
    ├── chargement assets (images SD)
    ├── préparation cache
    └── initialisation lourde
          ↓
activate()
    │
    └── préparation d'état temporaire (réinitialisation chrono, etc.)
          ↓
update()
    │
    └── logique temps réel (60 FPS) - **AUCUNE ALLOCATION DYNAMIQUE INUTILE**
          ↓
render()
    │
    └── rendu temps réel (60 FPS) - **AUCUNE ALLOCATION DYNAMIQUE INUTILE**
          ↓
deactivate()
    │
    └── libération de ressources externes ou arrêt des écouteurs
```

- **Règle d'or :** Ne créez jamais de nouveaux `String`, `std::vector` ou n'utilisez jamais `malloc`/`new` dans `update()` ou `render()`. Pré-allouez vos tampons dans `initialize()` et mutez-les en place.
- **`onConfigChanged()` :** Permet au moteur de mettre à jour son état interne lorsque l'utilisateur change les réglages via l'interface Web (asynchrone sur le Core 0).
- **`isFinished()` :** Utile pour signaler au `RotationManager` qu'un moteur a terminé sa tâche pour forcer le passage au moteur suivant sans attendre la fin du temps imparti.

---

## 2. Tutoriel : Créer un Nouveau Moteur (Engine)

Pour créer un nouveau moteur, vous devez implémenter l'interface `IEngine` et fournir un `EngineDescriptor` via le `EngineRegistry`.

### Étape 1 : Créer le header (`src/engines/MyEngine.h`)

```cpp
#pragma once
#include "core/engine_contract.h"
#include <Arduino.h>

class MyEngine : public IEngine {
public:
    MyEngine();
    virtual ~MyEngine() = default;

    void initialize(ApplicationContext* context, DictionaryEngineConfig* config) override;
    void activate() override;
    void update(ApplicationContext* context) override;
    void render(ApplicationContext* context) override;
    void deactivate() override;
    void onConfigChanged(DictionaryEngineConfig* config) override;
    bool isFinished() const override;

private:
    String mySetting;
    int counter;
};
```

### Étape 2 : Implémenter le cycle de vie (`src/engines/MyEngine.cpp`)

Implémentez le comportement de votre moteur, en respectant la contrainte d'allocation.

```cpp
#include "MyEngine.h"
#include "core/EngineRegistry.h"

MyEngine::MyEngine() : counter(0) {}

void MyEngine::initialize(ApplicationContext* context, DictionaryEngineConfig* config) {
    // Allocation mémoire, lecture des réglages
    mySetting = config->getString("my_setting", "default");
    Serial.println("MyEngine initialisé !");
}

void MyEngine::activate() {
    counter = 0; // Réinitialisation rapide
}

void MyEngine::update(ApplicationContext* context) {
    // Logique métier rapide, aucune allocation
    counter++;
}

void MyEngine::render(ApplicationContext* context) {
    // Rendu matériel via context->display
    context->display->fillScreen(0);
    context->display->setCursor(0, 0);
    context->display->print(mySetting.c_str()); // Pas de construction de String ici !
}

void MyEngine::deactivate() {
}

void MyEngine::onConfigChanged(DictionaryEngineConfig* config) {
    mySetting = config->getString("my_setting", "default");
}

bool MyEngine::isFinished() const {
    return false;
}
```

### Étape 3 : Enregistrer le Moteur au démarrage

Ouvrez le fichier `src/main.cpp` (ou l'endroit centralisé d'initialisation du Registry) et ajoutez votre descripteur pour exposer les champs de configuration à la Web UI :

```cpp
#include "engines/MyEngine.h"

// Dans setup()
EngineDescriptor myDesc;
myDesc.id = "my_engine";
myDesc.name = "Mon Moteur Custom";
myDesc.category = "divers";
myDesc.version = "1.0";

ConfigField field;
field.id = "my_setting";
field.type = ConfigType::String;
field.label = "Mon Réglage";
field.description = "Saisissez un mot à afficher";
myDesc.schema.fields.push_back(field);

myDesc.factory = []() -> std::unique_ptr<IEngine> {
    return std::unique_ptr<IEngine>(new MyEngine());
};

EngineRegistry::registerEngine(myDesc);
```

C'est tout ! **Aucun code du RotationManager n'a besoin d'être modifié**. Le moteur sera automatiquement listé dans l'API Web, et sa configuration `config.json` sera gérée de manière isolée via le `ConfigSchema`.

---

## 3. Limites Connues & Sécurité

### Sécurité
- **API non authentifiée** : L'API REST HTTP s'exécute sans authentification sur le réseau local. Ne pas exposer directement le port 80 à Internet.

### Limites Connues
- **Pas de rollback OTA automatique** : En cas de flashage d'un firmware avec un boot-loop, la restauration nécessite un re-flashage physique.
- **Réseau Synchrone dans les Providers** : Les clients HTTP externes peuvent être bloquants. Bien que gérés, il est conseillé de limiter les appels réseau agressifs.
- **Carte SD requise pour les assets lourds** : Les animations `.fgt` et polices `.amf` nécessitent impérativement une carte SD installée et formatée.

---

## 4. Ajouter une nouvelle cible matérielle (Hardware Profile)

Bien qu'ArcadeMatrix soit pensé pour optimiser les performances des cartes ESP32 standards, le projet supporte nativement des cartes plus musclées (ex: **ESP32-S3** avec 32 Mo de Flash et 16 Mo de PSRAM).

Si vous souhaitez porter ArcadeMatrix sur une nouvelle carte (avec un brochage différent ou un autre type de mémoire), vous devez créer un nouveau profil matériel. L'injection statique via les flags de compilation est la méthode privilégiée.

### Étape 1 : Définir le profil (`include/HardwareProfile.h`)
Ajoutez un nouveau bloc pour définir les broches de votre matrice HUB75 et de votre carte SD.
```cpp
#elif defined(HARDWARE_PROFILE_MON_ESP_S3)
    // Profil : ESP32-S3 avec PSRAM
    #define MATRIX_R1_PIN 10
    #define MATRIX_G1_PIN 11
    // ... définissez toutes les broches de la matrice ...
    
    // SD Card
    #define USE_SD_MMC 1
    #define SD_MMC_D0_PIN 12
    #define SD_MMC_CMD_PIN 13
    #define SD_MMC_CLK_PIN 14
```

### Étape 2 : Créer l'environnement (`platformio.ini`)
Ajoutez un nouvel environnement pour activer la PSRAM et injecter votre flag :
```ini
[env:mon_esp_s3]
board = esp32-s3-devkitc-1
build_flags = 
    -D HARDWARE_PROFILE_MON_ESP_S3
    -D BOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
```
*Note : Le code C++ allouera alors automatiquement les polices (AMF) ou les buffers mémoire étendus dans la PSRAM via `ps_malloc` lorsque disponible.*

### Tableau de Compatibilité des Moteurs (Matériel)

Tous les moteurs ne peuvent pas tourner sur un ESP32 classique à cause du manque de mémoire RAM. Assurez-vous de documenter ces pré-requis pour les utilisateurs.

| Moteur (`Engine`) | ESP32 (320 Ko) | ESP32-S3 (+PSRAM) | Remarques |
| :--- | :---: | :---: | :--- |
| `ClockEngine` | ✅ Oui | ✅ Oui | Logique légère, très peu d'allocations. |
| `MessageEngine` | ✅ Oui | ✅ Oui | Défilement texte. |
| `CryptoEngine` | ❌ Non | ✅ Oui | Stocke des graphiques historiques (tableaux de `float`) et analyse d'énormes payloads JSON API nécessitant `ps_malloc`. |
| `StockEngine` | ❌ Non | ✅ Oui | Pareil que `CryptoEngine`. |
| `FighterEngine` | ✅ Oui | ✅ Oui | Lit directement la carte SD en flux binaire `.fgt` sans charger l'image entière en RAM. |
