/**
 * @file matrix_generator.cpp
 * @brief Native CLI Tool: Evaluates all ArcadeMatrix engines against hardware reference profiles
 * using the canonical C++ CompatibilityEvaluator runtime engine.
 *
 * Emits structured JSON to stdout for documentation generators and CI integrity checks.
 */

#include <iostream>
#include <vector>
#include <string>
#include "Arduino.h"
#include <ArduinoJson.h>
#include "core/CompatibilityEvaluator.h"
#include "core/EngineRegistry.h"
#include "core/EngineContract.h"
#include "core/Logger.h"

extern void setMockProfile(HwProfile p);

struct ProfileDef {
    const char* id;
    const char* name;
    HwProfile profile;
    uint16_t width;
    uint16_t height;
    uint8_t colorDepth;
    bool hasPsram;
    size_t psramBytes;
    bool hasMic;
    bool hasTemp;
    bool hasGyro;
    bool hasNetwork;
    bool hasSd;
    size_t freeInternalHeap;
    size_t largestInternalBlock;
    size_t freePsram;
};

static const std::vector<ProfileDef> REFERENCE_PROFILES = {
    {
        "esp32_dev_128x32",
        "ESP32 Classic (128x32, No PSRAM)",
        HwProfile::ESP32_STD,
        128, 32, 0,
        false, 0,
        false, true, false, true, true,
        140000, 70000, 0
    },
    {
        "esp32_dev_64x32",
        "ESP32 Classic (64x32, No PSRAM)",
        HwProfile::ESP32_STD,
        64, 32, 0,
        false, 0,
        false, true, false, true, true,
        160000, 80000, 0
    },
    {
        "esp32_dev_128x64",
        "ESP32 Classic (128x64, No PSRAM)",
        HwProfile::ESP32_STD,
        128, 64, 0,
        false, 0,
        false, true, false, true, true,
        110000, 50000, 0
    },
    {
        "esp32s3_waveshare_128x32",
        "ESP32-S3 Waveshare (128x32, 8MB PSRAM)",
        HwProfile::WAVESHARE_S3,
        128, 32, 0,
        true, 8388608,
        true, true, true, true, true,
        230000, 115000, 7500000
    },
    {
        "esp32s3_waveshare_256x64",
        "ESP32-S3 Waveshare (256x64, 8MB PSRAM)",
        HwProfile::WAVESHARE_S3,
        256, 64, 0,
        true, 8388608,
        true, true, true, true, true,
        220000, 110000, 7340032
    }
};

static std::vector<EngineDescriptor> getCanonicalEngineDescriptors() {
    std::vector<EngineDescriptor> engines;

    // 1. Clock
    {
        EngineDescriptor d;
        d.metadata = {"clock", "Clock Engine", "info", FIRMWARE_VERSION};
        d.requirements.targetFps = 60;
        d.requirements.prefersDoubleBuffer = true;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 8000;
        d.requirements.internalContiguousBytes = 16000;
        engines.push_back(d);
    }
    // 2. Weather
    {
        EngineDescriptor d;
        d.metadata = {"weather", "Live Weather", "info", FIRMWARE_VERSION};
        d.requirements.needsNetwork = true;
        d.requirements.targetFps = 10;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 6000;
        d.requirements.internalContiguousBytes = 12000;
        engines.push_back(d);
    }
    // 3. Message
    {
        EngineDescriptor d;
        d.metadata = {"message", "Live Ticker & Quotes", "text", FIRMWARE_VERSION};
        d.requirements.needsNetwork = true;
        d.requirements.targetFps = 60;
        d.requirements.prefersDoubleBuffer = true;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 6000;
        d.requirements.internalContiguousBytes = 8000;
        engines.push_back(d);
    }
    // 4. AudioVisualizer
    {
        EngineDescriptor d;
        d.metadata = {"audiovisualizer", "Real-time Audio Spectrum", "audio", FIRMWARE_VERSION};
        d.requirements.needsAudioInput = true;
        d.requirements.needsAudio = true;
        d.requirements.targetFps = 60;
        d.requirements.prefersDoubleBuffer = true;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 12000;
        d.requirements.internalContiguousBytes = 24000;
        engines.push_back(d);
    }
    // 5. Decibel
    {
        EngineDescriptor d;
        d.metadata = {"decibel", "Audio Sound Level Meter", "audio", FIRMWARE_VERSION};
        d.requirements.needsAudioInput = true;
        d.requirements.needsAudio = true;
        d.requirements.targetFps = 30;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 4000;
        d.requirements.internalContiguousBytes = 8000;
        engines.push_back(d);
    }
    // 6. Crypto
    {
        EngineDescriptor d;
        d.metadata = {"crypto", "Crypto Ticker & Fear/Greed", "finance", FIRMWARE_VERSION};
        d.requirements.needsNetwork = true;
        d.requirements.needsTls = true;
        d.requirements.targetFps = 10;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 8000;
        d.requirements.internalContiguousBytes = 16000;
        engines.push_back(d);
    }
    // 7. Stock
    {
        EngineDescriptor d;
        d.metadata = {"stock", "Stock Ticker & Indices", "finance", FIRMWARE_VERSION};
        d.requirements.needsNetwork = true;
        d.requirements.needsTls = true;
        d.requirements.targetFps = 10;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 8000;
        d.requirements.internalContiguousBytes = 16000;
        engines.push_back(d);
    }
    // 8. Date
    {
        EngineDescriptor d;
        d.metadata = {"date", "Date Display", "info", FIRMWARE_VERSION};
        d.requirements.targetFps = 30;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 3000;
        engines.push_back(d);
    }
    // 9. GIF Player
    {
        EngineDescriptor d;
        d.metadata = {"gifs", "GIF Player", "media", FIRMWARE_VERSION};
        d.requirements.needsSd = true;
        d.requirements.targetFps = 60;
        d.requirements.prefersDoubleBuffer = true;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 12000;
        d.requirements.internalContiguousBytes = 16000;
        d.requirements.shadowBytesPerFrame = 8192;
        engines.push_back(d);
    }
    // 10. Environment Sensor (Temperature & Humidity)
    {
        EngineDescriptor d;
        d.metadata = {"temp", "Environment Sensor", "sensor", FIRMWARE_VERSION};
        d.requirements.needsTempSensor = false; // Graceful fallback if sensor absent
        d.requirements.targetFps = 30;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 4000;
        engines.push_back(d);
    }
    // 11. GoogleCast
    {
        EngineDescriptor d;
        d.metadata = {"google_cast", "Google Nest Cast Audio", "media", FIRMWARE_VERSION};
        d.requirements.needsNetwork = true;
        d.requirements.targetFps = 30;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 16000;
        d.requirements.internalContiguousBytes = 24000;
        engines.push_back(d);
    }
    // 12. Spotify
    {
        EngineDescriptor d;
        d.metadata = {"spotify", "Spotify Player", "media", FIRMWARE_VERSION};
        d.requirements.needsNetwork = true;
        d.requirements.needsTls = true;
        d.requirements.targetFps = 30;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 14000;
        d.requirements.internalContiguousBytes = 24000;
        engines.push_back(d);
    }
    // 13. SysInfo
    {
        EngineDescriptor d;
        d.metadata = {"system_info", "System Monitor", "system", FIRMWARE_VERSION};
        d.requirements.targetFps = 10;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 3000;
        engines.push_back(d);
    }
    // 14. Music
    {
        EngineDescriptor d;
        d.metadata = {"music_player", "Universal Music Player", "media", FIRMWARE_VERSION};
        d.requirements.targetFps = 30;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 5000;
        engines.push_back(d);
    }
    // 15. Dashboard
    {
        EngineDescriptor d;
        d.metadata = {"dashboard", "Dashboard Engine", "info", FIRMWARE_VERSION};
        d.requirements.targetFps = 10;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 6000;
        engines.push_back(d);
    }
    // 16. GNews
    {
        EngineDescriptor d;
        d.metadata = {"gnews", "GNews Live Feed", "news", FIRMWARE_VERSION};
        d.requirements.needsPsram = false;
        d.requirements.needsNetwork = true;
        d.requirements.needsTls = true;
        d.requirements.targetFps = 30;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 12000;
        d.requirements.internalContiguousBytes = 16000;
        d.requirements.psramBytes = 0;
        engines.push_back(d);
    }
    // 17. Marquee
    {
        EngineDescriptor d;
        d.metadata = {"marquee", "Gameroom Marquee", "arcade", FIRMWARE_VERSION};
        d.requirements.needsSd = true;
        d.requirements.targetFps = 60;
        d.requirements.prefersDoubleBuffer = true;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 8000;
        d.requirements.internalContiguousBytes = 16000;
        engines.push_back(d);
    }
    // 18. MQTT Data
    {
        EngineDescriptor d;
        d.metadata = {"mqttdata", "MQTT Data", "info", FIRMWARE_VERSION};
        d.requirements.needsNetwork = true;
        d.requirements.targetFps = 30;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 4000;
        d.requirements.internalContiguousBytes = 8000;
        engines.push_back(d);
    }

    return engines;
}

int main() {
    Logger::setLevel(LOG_LEVEL_NONE);
    auto engines = getCanonicalEngineDescriptors();

    DynamicJsonDocument doc(131072);
    JsonArray profilesArr = doc.createNestedArray("profiles");
    for (const auto& p : REFERENCE_PROFILES) {
        JsonObject pObj = profilesArr.createNestedObject();
        pObj["id"] = p.id;
        pObj["name"] = p.name;
        pObj["width"] = p.width;
        pObj["height"] = p.height;
        pObj["has_psram"] = p.hasPsram;
        pObj["has_microphone"] = p.hasMic;
        pObj["has_temp_sensor"] = p.hasTemp;
        pObj["has_gyroscope"] = p.hasGyro;
    }

    JsonArray enginesArr = doc.createNestedArray("engines");

    for (const auto& eng : engines) {
        JsonObject engObj = enginesArr.createNestedObject();
        engObj["id"] = eng.metadata.id;
        engObj["name"] = eng.metadata.name;
        engObj["category"] = eng.metadata.category;
        engObj["target_fps"] = eng.requirements.targetFps;

        JsonObject reqObj = engObj.createNestedObject("requirements");
        reqObj["needs_psram"] = eng.requirements.needsPsram;
        reqObj["needs_audio_input"] = eng.requirements.needsAudioInput;
        reqObj["needs_temp_sensor"] = eng.requirements.needsTempSensor;
        reqObj["needs_gyroscope"] = eng.requirements.needsGyroscope;
        reqObj["needs_network"] = eng.requirements.needsNetwork;
        reqObj["needs_tls"] = eng.requirements.needsTls;
        reqObj["needs_sd"] = eng.requirements.needsSd;
        reqObj["requires_double_buffer"] = eng.requirements.requiresDoubleBuffer;
        reqObj["prefers_double_buffer"] = eng.requirements.prefersDoubleBuffer;
        reqObj["supports_single_buffer"] = eng.requirements.supportsSingleBuffer;
        reqObj["internal_persistent_bytes"] = eng.requirements.internalPersistentBytes;
        reqObj["internal_contiguous_bytes"] = eng.requirements.internalContiguousBytes;
        reqObj["psram_bytes"] = eng.requirements.psramBytes;

        JsonObject evalsObj = engObj.createNestedObject("evaluations");

        for (const auto& p : REFERENCE_PROFILES) {
            setMockProfile(p.profile);
            CompatibilityContext ctx;
            ctx.width = p.width;
            ctx.height = p.height;
            ctx.colorDepth = p.colorDepth;
            ctx.requestedPipeline = "auto";
            ctx.isConnectedWifi = p.hasNetwork;

            ctx.hardware.hasPsram = p.hasPsram;
            ctx.hardware.psramBytes = p.psramBytes;
            ctx.hardware.hasMicrophone = p.hasMic;
            ctx.hardware.hasTempSensor = p.hasTemp;
            ctx.hardware.hasGyroscope = p.hasGyro;
            ctx.hardware.hasNetwork = p.hasNetwork;
            ctx.hardware.hasSd = p.hasSd;
            ctx.hardware.profile = p.profile;

            ctx.memory.freeInternalHeap = p.freeInternalHeap;
            ctx.memory.largestInternalBlock = p.largestInternalBlock;
            ctx.memory.freePsram = p.freePsram;

            // Execute canonical C++ CompatibilityEvaluator
            CompatibilityVerdict verdict = CompatibilityEvaluator::evaluate(eng, ctx);

            JsonObject vObj = evalsObj.createNestedObject(p.id);
            vObj["status"] = CompatibilityEvaluator::statusToString(verdict.status);
            vObj["compatible"] = verdict.compatible();
            vObj["degraded"] = verdict.degraded();
            vObj["primary_reason"] = CompatibilityEvaluator::reasonToString(verdict.primaryReason);
            vObj["strategy"] = CompatibilityEvaluator::strategyToString(verdict.strategy);
            vObj["target_fps"] = verdict.targetFps;
            vObj["estimated_fps"] = verdict.estimatedPresentationFps;
            vObj["validated_fps"] = verdict.validatedFps;
            vObj["empirically_validated"] = verdict.empiricallyValidated;
            vObj["issue_flags"] = verdict.issueFlags;

            JsonObject memObj = vObj.createNestedObject("memory");
            memObj["internal_required"] = verdict.internalRequiredBytes;
            memObj["internal_available"] = verdict.internalAvailableBytes;
            memObj["internal_headroom"] = verdict.internalHeadroomBytes;
            memObj["largest_required_block"] = verdict.largestRequiredBlockBytes;
            memObj["largest_available_block"] = verdict.largestAvailableBlockBytes;
            memObj["psram_required"] = verdict.psramRequiredBytes;
            memObj["psram_available"] = verdict.psramAvailableBytes;
        }
    }

    serializeJsonPretty(doc, Serial);
    Serial.println();
    return 0;
}
