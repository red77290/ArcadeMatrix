[English](DEVELOPER.md) | 🇫🇷 Français | 🇪🇸 [Español](DEVELOPER_ES.md)

# Guide Développeur (ESP32 — C++)

Bienvenue dans le guide développeur d'ArcadeMatrix pour ESP32. Ce document explique comment créer de nouveaux moteurs, déclarer des schémas de configuration, définir les prérequis matériels et s'intégrer proprement à l'interface Web dynamique.

---

## 1. Architecture des Moteurs & Cycle de Vie Strict

ArcadeMatrix repose sur une architecture découplée :
1. **`IEngine`** : Contrat d'interface abstrait pour tous les modules d'affichage.
2. **`EngineRegistry`** : Registre centralisé stockant les descripteurs et fabriques.
3. **`EngineRegistrar`** : Point unique de gating évaluant les capacités réelles du `HardwareHAL`.
4. **`ConfigSanitizer`** : Validation déclarative et injection des valeurs par défaut.
5. **`RotationManager`** : Instanciation paresseuse (lazy) et boucle de rotation.

```text
initialize() [Init unique & allocation mémoire]
      ↓
activate() [Reset temporisateurs / état au switch]
      ↓
update() [Calcul logique - 60 FPS - ZÉRO allocation heap]
      ↓
render() [Dessin des pixels sur MatrixPanel_I2S_DMA]
      ↓
deactivate() [Mise en veille / fermeture fichiers / stop audio]
```

### Règles Fondamentales
- **Zéro allocation dans la boucle active** : Ne jamais instancier de `String`, `std::vector`, ou faire de `malloc`/`new` dans `update()` ou `render()`. Pré-allouez vos tampons dans `initialize()`.
- **Hot Reload Live** : Implémentez `onConfigChanged(const EngineConfig* config)` pour répercuter les réglages utilisateur sans aucun redémarrage.
- **Isolation Matérielle** : Ne mettez jamais d'appels `psramFound()` ou `#ifdef BOARD_HAS_PSRAM` dans votre moteur. Déclarez vos besoins dans `requirements.needsPsram`.

---

## 2. Tutoriel : Créer un Nouveau Moteur

### Étape 1 : Déclarer la classe (`src/engines/MyEngine.h`)

```cpp
#pragma once
#include "../../include/core/EngineContract.h"
#include <Arduino.h>

class MyEngine : public IEngine {
public:
    MyEngine();
    ~MyEngine() override = default;

    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* config) override;
    
    // Méthodes optionnelles (valeurs sûres par défaut fournies) :
    bool isFinished() const override { return false; }
    bool isRealtime() const override { return true; }
    bool selfPaced() const override { return false; }
    bool allowsOverlay() const override { return true; }

private:
    MatrixPanel_I2S_DMA* matrix = nullptr;
    int speed = 1;
    String text = "Hello";
    int posX = 0;
};
```

### Étape 2 : Implémenter le comportement (`src/engines/MyEngine.cpp`)

```cpp
#include "MyEngine.h"
#include "../core/Logger.h"

MyEngine::MyEngine() {}

EngineError MyEngine::initialize(EngineContext* context, const EngineConfig* config) {
    if (!context || !context->getMatrix()) return EngineError::InitializationFailed;
    matrix = context->getMatrix();

    if (config) {
        speed = config->getInt("speed", 1);
        text = config->getString("text", "Hello");
    }
    LOGI("MyEngine", "Initialisé avec succès");
    return EngineError::OK;
}

void MyEngine::activate() {
    posX = 0;
}

void MyEngine::update(EngineContext* context) {
    posX += speed;
    if (matrix && posX > matrix->width()) {
        posX = -50;
    }
}

void MyEngine::render(EngineContext* context) {
    if (!matrix) return;
    matrix->fillScreen(0);
    matrix->setCursor(posX, 10);
    matrix->print(text);
}

void MyEngine::deactivate() {
    // Nettoyage des ressources temporaires
}

void MyEngine::onConfigChanged(const EngineConfig* config) {
    if (config) {
        speed = config->getInt("speed", 1);
        text = config->getString("text", "Hello");
    }
}
```

### Étape 3 : Enregistrer le moteur dans `src/engines/EngineRegistrar.cpp`

Ajoutez votre descripteur dans `EngineRegistrar::registerAll()` :

```cpp
#include "MyEngine.h"

void EngineRegistrar::registerAll() {
    // ...
    EngineDescriptor desc;
    desc.metadata = {
        .id = "my_engine",
        .name = "Mon Moteur Personnalisé",
        .category = "custom",
        .version = "1.0.0"
    };
    desc.capabilities = {
        .supports_128x32 = true,
        .supports_256x64 = true,
        .realtime = true,
        .interruptible = true,
        .allowsOverlay = true,
        .selfPaced = false
    };
    desc.requirements = {
        .needsPsram = false,
        .needsAudio = false,
        .needsTempSensor = false,
        .needsGyroscope = false,
        .needsNetwork = false,
        .needsSd = false
    };
    desc.schema.fields = {
        {
            .id = "speed",
            .type = ConfigType::INTEGER,
            .label = "Vitesse de défilement",
            .description = "Pixels par frame",
            .default_value = "1",
            .required = false,
            .min_val = "1",
            .max_val = "10",
            .step = "1",
            .validation_policy = ValidationPolicy::Clamp
        },
        {
            .id = "text",
            .type = ConfigType::STRING,
            .label = "Texte affiché",
            .description = "Message à faire défiler",
            .default_value = "Bonjour le monde",
            .required = false
        }
    };
    desc.factory = []() {
        return std::unique_ptr<IEngine>(new MyEngine());
    };

    tryRegister(desc);
}
```

---

## 3. Types de Champs & Options Dynamiques

| `ConfigType` | Rendu dans l'UI Web | Attributs Supportés |
|---|---|---|
| `BOOLEAN` | Menu déroulant (Activé / Désactivé) | `default_value` |
| `INTEGER` | Champ numérique borné | `min_val`, `max_val`, `step`, `validation_policy` |
| `FLOAT` | Champ décimal | `min_val`, `max_val`, `step`, `validation_policy` |
| `STRING` | Champ texte | `default_value` |
| `ENUM` | Menu déroulant | `options="opt1,opt2"`, `options_endpoint` |
| `COLOR` | Sélecteur de couleur HTML5 | `default_value="#ffffff"` |
| `LIST` | Sélection multiple | `options_endpoint="/api/playlists"`, `multiple=true` |

### Exemple avec Endpoint d'Options Dynamiques
```cpp
{
    .id = "theme",
    .type = ConfigType::ENUM,
    .label = "Thème de l'horloge",
    .default_value = "0",
    .options_endpoint = "/api/themes"
}
```

---

## 4. Gating des Prérequis Matériels

Si votre moteur nécessite du matériel spécifique (ex: micro ou PSRAM) :
```cpp
desc.requirements.needsPsram = true;
desc.requirements.needsAudio = true;
```

`EngineRegistrar` évalue automatiquement `HardwareHAL::capabilities()`. Si la carte branchée ne dispose pas de ce périphérique, le moteur est ignoré proprement et l'UI affiche un badge explicatif sans aucun crash.
