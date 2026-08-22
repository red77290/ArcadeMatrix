🇬🇧 English | 🇫🇷 [Français](DEVELOPER_FR.md) | 🇪🇸 [Español](DEVELOPER_ES.md)

# Developer Guide (ESP32 — C++)

Welcome to the ArcadeMatrix development guide for ESP32. This document explains how to create new engines, declare configuration schemas, configure hardware requirements, and integrate cleanly with the dynamic WebUI.

---

## 1. The Engine Architecture & Strict Lifecycle

ArcadeMatrix uses an open, decoupled architecture:
1. **`IEngine`**: Abstract contract for display modules.
2. **`EngineRegistry`**: Storage for engine descriptors and factories.
3. **`EngineRegistrar`**: Central gating point checking `HardwareHAL` runtime capabilities.
4. **`ConfigSanitizer`**: Declarative schema validation and auto-injection.
5. **`RotationManager`**: Dynamic lazy instantiation and loop orchestration.

```text
initialize() [One-time setup & memory allocation]
      ↓
activate() [Reset timers/state on rotation switch]
      ↓
update() [Compute logic - 60 FPS - ZERO heap allocations]
      ↓
render() [Draw pixels to MatrixPanel_I2S_DMA]
      ↓
deactivate() [Standby / release files / stop audio]
```

### Critical Rules
- **Zero Allocations in Hot Loop**: Never instantiate `String`, `std::vector`, or call `malloc`/`new` in `update()` or `render()`. Pre-allocate all buffers in `initialize()`.
- **Live Hot Reload**: Implement `onConfigChanged(const EngineConfig* config)` to apply user changes immediately without requiring a reboot.
- **Hardware Isolation**: Never call `psramFound()` or `#ifdef BOARD_HAS_PSRAM` inside your engine. Declare your needs in `requirements.needsPsram` instead.

---

## 2. Step-by-Step: Creating a New Engine

### Step 1: Declare the Engine Class (`src/engines/MyEngine.h`)

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
    
    // Optional overrides (default safe behaviors provided):
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

### Step 2: Implement the Engine Logic (`src/engines/MyEngine.cpp`)

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
    LOGI("MyEngine", "Initialized successfully");
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
    // Clean up temporary active resources
}

void MyEngine::onConfigChanged(const EngineConfig* config) {
    if (config) {
        speed = config->getInt("speed", 1);
        text = config->getString("text", "Hello");
    }
}
```

### Step 3: Register in `src/engines/EngineRegistrar.cpp`

Add your engine descriptor to `EngineRegistrar::registerAll()`:

```cpp
#include "MyEngine.h"

void EngineRegistrar::registerAll() {
    // ...
    EngineDescriptor desc;
    desc.metadata = {
        .id = "my_engine",
        .name = "My Custom Engine",
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
            .label = "Scroll Speed",
            .description = "Speed in pixels per frame",
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
            .label = "Display Text",
            .description = "Text message to scroll",
            .default_value = "Hello World",
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

## 3. Schema Data Types & Dynamic Options

| `ConfigType` | Rendered UI Widget | Attributes Supported |
|---|---|---|
| `BOOLEAN` | Dropdown (Enabled / Disabled) | `default_value` |
| `INTEGER` | Number input with bounds | `min_val`, `max_val`, `step`, `validation_policy` |
| `FLOAT` | Decimal number input | `min_val`, `max_val`, `step`, `validation_policy` |
| `STRING` | Text input | `default_value` |
| `ENUM` | Dropdown select | `options="opt1,opt2"`, `options_endpoint` |
| `COLOR` | HTML5 Color picker | `default_value="#ffffff"` |
| `LIST` | Multi-select dropdown | `options_endpoint="/api/playlists"`, `multiple=true` |

### Dynamic Options Endpoint Example
To populate a select dropdown from firmware API endpoints:
```cpp
{
    .id = "theme",
    .type = ConfigType::ENUM,
    .label = "Clock Theme",
    .default_value = "0",
    .options_endpoint = "/api/themes"
}
```

---

## 4. Hardware Capability Gating

If your engine requires specific peripherals (e.g. microphone or PSRAM):
```cpp
desc.requirements.needsPsram = true;
desc.requirements.needsAudio = true;
```

`EngineRegistrar` automatically evaluates `HardwareHAL::capabilities()`. If requirements are not satisfied on the connected board, the engine is skipped and the WebUI displays an informative unavailable badge without crashing.
