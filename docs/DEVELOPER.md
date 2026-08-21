🇬🇧 English | 🇫🇷 [Français](DEVELOPER_FR.md) | 🇪🇸 [Español](DEVELOPER_ES.md)

# Developer Guide (ESP32 - C++)

Welcome to the ArcadeMatrix development guide for ESP32. This document explains how to extend the architecture and create new Engines in C++.

---

## 1. Understanding the Architecture: Engines, Registry, and Lifecycle

ArcadeMatrix no longer has a hardcoded list of its features. The system relies on a **Registry** that discovers engines at startup.

### 1.1 The Strict Lifecycle (Lazy-Once)

The ESP32 has an extremely limited Heap (approx. 320 KB). To avoid crashes (Kernel Panics) due to memory fragmentation, ArcadeMatrix enforces a strict lifecycle for each `IEngine`.

```text
initialize()
    │
    ├── allocation via 'new' or 'std::vector'
    ├── loading assets (SD images)
    ├── cache preparation
    └── heavy initialization
          ↓
activate()
    │
    └── temporary state preparation (chronometer reset, etc.)
          ↓
update()
    │
    └── real-time logic (60 FPS) - **NO UNNECESSARY DYNAMIC ALLOCATIONS**
          ↓
render()
    │
    └── real-time rendering (60 FPS) - **NO UNNECESSARY DYNAMIC ALLOCATIONS**
          ↓
deactivate()
    │
    └── freeing external resources or stopping listeners
```

- **Golden rule:** Never create new `String`, `std::vector`, or use `malloc`/`new` inside `update()` or `render()`. Pre-allocate your buffers in `initialize()` and mutate them in place.
- **`onConfigChanged()`:** Allows the engine to update its internal state when the user changes settings via the Web UI (asynchronous on Core 0).
- **`isFinished()`:** Useful to signal the `RotationManager` that an engine has finished its task to force moving to the next engine without waiting for the timeout.

---

## 2. Tutorial: Creating a New Engine

To create a new engine, you must implement the `IEngine` interface and provide an `EngineDescriptor` via the `EngineRegistry`.

### Step 1: Create the header (`src/engines/MyEngine.h`)

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

### Step 2: Implement the Lifecycle (`src/engines/MyEngine.cpp`)

Implement your engine's behavior, respecting the allocation constraint.

```cpp
#include "MyEngine.h"
#include "core/EngineRegistry.h"

MyEngine::MyEngine() : counter(0) {}

void MyEngine::initialize(ApplicationContext* context, DictionaryEngineConfig* config) {
    // Memory allocation, reading settings
    mySetting = config->getString("my_setting", "default");
    Serial.println("MyEngine initialized!");
}

void MyEngine::activate() {
    counter = 0; // Quick reset
}

void MyEngine::update(ApplicationContext* context) {
    // Fast business logic, NO allocations
    counter++;
}

void MyEngine::render(ApplicationContext* context) {
    // Hardware rendering via context->display
    context->display->fillScreen(0);
    context->display->setCursor(0, 0);
    context->display->print(mySetting.c_str()); // No String construction here!
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

### Step 3: Register the Engine at Startup

Open the `src/main.cpp` file (or the centralized Registry initialization location) and add your descriptor to expose configuration fields to the Web UI:

```cpp
#include "engines/MyEngine.h"

// In setup()
EngineDescriptor myDesc;
myDesc.id = "my_engine";
myDesc.name = "My Custom Engine";
myDesc.category = "misc";
myDesc.version = "1.0";

ConfigField field;
field.id = "my_setting";
field.type = ConfigType::String;
field.label = "My Setting";
field.description = "Enter a word to display";
myDesc.schema.fields.push_back(field);

myDesc.factory = []() -> std::unique_ptr<IEngine> {
    return std::unique_ptr<IEngine>(new MyEngine());
};

EngineRegistry::registerEngine(myDesc);
```

That's it! **No `RotationManager` code needs to be modified**. The engine will be automatically listed in the Web API, and its `config.json` configuration will be managed in an isolated way via the `ConfigSchema`.

---

## 3. Known Limitations & Security

### Security
- **Unauthenticated API:** The HTTP REST API runs without authentication on the local network. Do not expose port 80 directly to the Internet.

### Known Limitations
- **No automatic OTA rollback:** In case of flashing a firmware with a boot-loop, restoration requires physical re-flashing.
- **Synchronous Network in Providers:** External HTTP clients can be blocking. Although managed, it is advisable to limit aggressive network calls.
- **SD Card required for heavy assets:** `.fgt` animations and `.amf` fonts strictly require an installed and formatted SD card.

---

## 4. Adding a new Hardware Profile

Although ArcadeMatrix is designed to optimize the performance of standard ESP32 boards, the project natively supports more powerful boards (e.g., **ESP32-S3** with 32 MB Flash and 16 MB PSRAM).

If you want to port ArcadeMatrix to a new board (with a different pinout or another type of memory), you must create a new hardware profile. Static injection via compilation flags is the preferred method.

### Step 1: Define the profile (`include/HardwareProfile.h`)
Add a new block to define the pins of your HUB75 matrix and your SD card.
```cpp
#elif defined(HARDWARE_PROFILE_MY_ESP_S3)
    // Profile: ESP32-S3 with PSRAM
    #define MATRIX_R1_PIN 10
    #define MATRIX_G1_PIN 11
    // ... define all matrix pins ...
    
    // SD Card
    #define USE_SD_MMC 1
    #define SD_MMC_D0_PIN 12
    #define SD_MMC_CMD_PIN 13
    #define SD_MMC_CLK_PIN 14
```

### Step 2: Create the environment (`platformio.ini`)
Add a new environment to enable PSRAM and inject your flag:
```ini
[env:my_esp_s3]
board = esp32-s3-devkitc-1
build_flags = 
    -D HARDWARE_PROFILE_MY_ESP_S3
    -D BOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
```
*Note: The C++ code will then automatically allocate fonts (AMF) or extended memory buffers in PSRAM via `ps_malloc` when available.*

### Hardware Engine Compatibility Table

Not all engines can run on a standard ESP32 due to lack of RAM memory. Be sure to document these requirements for users.

| Engine (`Engine`) | ESP32 (320 KB) | ESP32-S3 (+PSRAM) | Notes |
| :--- | :---: | :---: | :--- |
| `ClockEngine` | ✅ Yes | ✅ Yes | Lightweight logic, very few allocations. |
| `MessageEngine` | ✅ Yes | ✅ Yes | Text scrolling. |
| `CryptoEngine` | ❌ No | ✅ Yes | Stores historical charts (`float` arrays) and parses huge API JSON payloads requiring `ps_malloc`. |
| `StockEngine` | ❌ No | ✅ Yes | Same as `CryptoEngine`. |
| `FighterEngine` | ✅ Yes | ✅ Yes | Reads the SD card directly in a binary `.fgt` stream without loading the whole image into RAM. |
