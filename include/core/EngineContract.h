#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>
#include <memory>
#include <time.h>

// Forward declarations to avoid heavy includes in the contract
class MatrixPanel_I2S_DMA;
class FrontendSyncEngine; // Represents EventBus/MQTT currently
// class Logger; // Could be added later

// =======================================================
// 1. Enums & Errors
// =======================================================

enum class EngineError {
    OK,
    InvalidConfig,
    MissingResource,
    InitializationFailed,
    RenderFailed,
    HardwareUnavailable,
    RuntimeError
};

enum class ConfigType {
    BOOLEAN,
    INTEGER,
    FLOAT,
    STRING,
    ENUM,
    COLOR,
    DURATION,
    LIST,
    FILE_ASSET
};

// =======================================================
// 2. Metadata & Capabilities
// =======================================================

struct EngineMetadata {
    const char* id;          // e.g., "clock.cyberpunk"
    const char* name;        // e.g., "Cyberpunk Clock"
    const char* category;    // e.g., "clock", "visualizer", "info"
    const char* version;     // e.g., "1.0.0"
};

struct EngineCapabilities {
    bool supports_128x32 = true;
    bool supports_256x64 = true;
    bool needs_audio = false;
    bool needs_network = false;
    bool needs_sd = false;
    bool realtime = false;
    bool interruptible = true;
};

// =======================================================
// 3. Configuration Schema
// =======================================================

struct ConfigField {
    const char* id;
    ConfigType type;
    const char* label;
    const char* description = "";
    const char* default_value = "";
    bool required = false;
    
    // Validation limits (strings, but can be parsed based on type)
    const char* min_val = "";
    const char* max_val = "";
    const char* step = "";
    
    // Comma-separated options for ENUM/LIST
    const char* options = "";
    
    // E.g. "mode=custom"
    const char* visible_when = "";
};

struct ConfigSchema {
    std::vector<ConfigField> fields;
};

// =======================================================
// 4. Engine Context (Dependency Injection)
// =======================================================

// A specific configuration for a specific instance of an engine
// This will eventually wrap a JSON object or an INI block.
class EngineConfig {
public:
    virtual ~EngineConfig() = default;
    virtual String getString(const char* key, const char* default_val = "") const = 0;
    virtual int getInt(const char* key, int default_val = 0) const = 0;
    virtual float getFloat(const char* key, float default_val = 0.0f) const = 0;
    virtual bool getBool(const char* key, bool default_val = false) const = 0;
};

// The context provides all runtime services needed by an Engine.
class EngineContext {
public:
    virtual ~EngineContext() = default;

    // Core matrix wrapper for drawing operations
    virtual MatrixPanel_I2S_DMA* getMatrix() = 0;
    
    // Optional Event Bus (MQTT / Batocera events)
    virtual FrontendSyncEngine* getEventBus() = 0;

    // Time services
    // Fetches the current local system time
    virtual void getSystemTime(struct tm* timeinfo) = 0;
};

// =======================================================
// 5. Engine Interface
// =======================================================

class IEngine {
public:
    virtual ~IEngine() = default;

    // Lifecycle
    virtual EngineError initialize(EngineContext* context, const EngineConfig* config) = 0;
    virtual void activate() = 0;
    virtual void update(EngineContext* context) = 0;
    virtual void render(EngineContext* context) = 0;
    virtual void deactivate() = 0;
    
    // Dynamic Configuration
    virtual void onConfigChanged(const EngineConfig* config) {}
    
    // Intrinsic sequence completion signaling (not tied to Rotation duration)
    virtual bool isFinished() const { return false; }
};

// =======================================================
// 6. Factory Definition
// =======================================================

using EngineFactory = std::function<std::unique_ptr<IEngine>()>;

struct EngineDescriptor {
    EngineMetadata metadata;
    EngineCapabilities capabilities;
    EngineCapabilities requirements;
    ConfigSchema schema;
    EngineFactory factory;
};
