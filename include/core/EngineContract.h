#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>
#include <memory>
#include <time.h>
#include "core/BuildInfo.h"

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

enum class ValidationPolicy {
    Clamp,
    FallbackDefault,
    Ignore
};

// =======================================================
// 2. Metadata & Capabilities
// =======================================================

struct EngineMetadata {
    const char* id = "";          // e.g., "clock"
    const char* name = "";        // e.g., "Clock"
    const char* category = "";    // e.g., "info", "media", "finance"
    const char* version = FIRMWARE_VERSION;

    EngineMetadata() = default;
    EngineMetadata(const char* id_, const char* name_, const char* category_, const char* ver_ = FIRMWARE_VERSION)
        : id(id_), name(name_), category(category_), version(ver_) {}
};

struct EngineCapabilities {
    bool supports_128x32 = true;
    bool supports_256x64 = true;
    bool realtime = false;
    bool interruptible = true;
    bool selfPaced = false;

    /**
     * @brief Declares whether this engine permits transverse overlays (such as the M.U.G.E.N Fighter)
     *        to be composited on top of its rendered frame.
     *        If false, overlays are permanently forbidden for this engine regardless of user settings.
     *        If true, active overlay display is governed by user settings (master switch + per-slot toggle).
     */
    bool allowsOverlay = true;

    /**
     * @brief Declares whether this engine can be selected by users as part of the normal display rotation loop.
     *        If false, the engine is strictly event-driven/preemptive (e.g. Marquee game covers, system alerts)
     *        and will not appear in the Web UI rotation picker or be permitted by the ConfigSanitizer.
     */
    bool allowRotation = true;
};

struct EngineRequirements {
    bool needsPsram = false;
    bool needsAudio = false;
    bool needsTempSensor = false;
    bool needsGyroscope = false;
    bool needsNetwork = false;
    bool needsSd = false;
};

// =======================================================
// 3. Configuration Schema
// =======================================================

struct ConfigField {
    const char* id = "";
    ConfigType type = ConfigType::STRING;
    const char* label = "";
    const char* description = "";
    const char* default_value = "";
    bool required = false;
    
    // Validation limits (strings, but can be parsed based on type)
    const char* min_val = "";
    const char* max_val = "";
    const char* step = "";
    
    // Comma-separated options for ENUM/LIST
    const char* options = "";
    
    // Dynamic endpoint for options (e.g. "/api/themes", "/api/fonts", "/api/playlists")
    const char* options_endpoint = "";
    bool multiple = false;
    
    // E.g. "mode=custom"
    const char* visible_when = "";
    
    ValidationPolicy validation_policy = ValidationPolicy::Clamp;

    ConfigField() = default;
    ConfigField(const char* id_, ConfigType type_, const char* label_, const char* desc_ = "", const char* def_ = "",
                bool req_ = false, const char* min_ = "", const char* max_ = "", const char* step_ = "",
                const char* opt_ = "", const char* opt_ep_ = "", bool mult_ = false, const char* vis_ = "",
                ValidationPolicy val_pol_ = ValidationPolicy::Clamp)
        : id(id_), type(type_), label(label_), description(desc_), default_value(def_),
          required(req_), min_val(min_), max_val(max_), step(step_),
          options(opt_), options_endpoint(opt_ep_), multiple(mult_), visible_when(vis_),
          validation_policy(val_pol_) {}
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

    // Hardware runtime services
    virtual bool hasPsram() const { return false; }
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

    // Realtime / dynamic cadence checking (for adaptive framerate)
    virtual bool isRealtime() const { return false; }

    // Self-paced engines (e.g. GIF player counting N items instead of seconds)
    virtual bool selfPaced() const { return false; }
    virtual void setRotationBudget(uint32_t budget) {}

    /**
     * @brief Declares whether this specific engine instance authorizes transverse overlays
     *        (like Fighter) to composite on top of its rendered pixels.
     *        Defaults to true. Can be overridden to false for full-screen emergency alerts, etc.
     */
    virtual bool allowsOverlay() const { return true; }

    /**
     * @brief Declares whether this engine is eligible for the normal user rotation sequence.
     *        Defaults to true. Returns false for purely event-driven/preemptive engines (e.g. Marquee).
     */
    virtual bool allowRotation() const { return true; }
};

// =======================================================
// 6. Factory Definition
// =======================================================

using EngineFactory = std::function<std::unique_ptr<IEngine>()>;

struct EngineDescriptor {
    EngineMetadata metadata;
    EngineCapabilities capabilities;
    EngineRequirements requirements;
    ConfigSchema schema;
    EngineFactory factory;
};

// =======================================================
// 7. Descriptor Handler Interface
// =======================================================

/**
 * @class IEngineDescriptorHandler
 * @brief Interface implemented by engines to declare their metadata, capabilities,
 *        hardware requirements, config schema, and factory constructor.
 */
class IEngineDescriptorHandler {
public:
    virtual ~IEngineDescriptorHandler() = default;
    virtual EngineDescriptor getDescriptor() const = 0;
};

using EngineDescriptorHandler = IEngineDescriptorHandler;

