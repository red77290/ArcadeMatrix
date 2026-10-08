#include "ClockEngine.h"
#include "../core/Logger.h"
#include "../core/ConfigLoader.h"
#include "clocks/ArcadeClock.h"
#include "clocks/CyberpunkClock.h"
#include "clocks/FlipClock.h"
#include "clocks/PongClock.h"
#include "clocks/TetrisClock.h"
#include "clocks/WordClock.h"
#include "clocks/BinaryClock.h"
#include "clocks/PacmanClock.h"
#include "clocks/MarioClock.h"
#include "clocks/CastleClock.h"
#if !defined(HARDWARE_PROFILE_ESP32_DEV)
#include "clocks/PokedexClock.h"
#include "clocks/WorldMapClock.h"
#include "clocks/WordsClock.h"
#include "clocks/MetalSlugClock.h"
#endif
#include "clocks/VersusClock.h"
#include "clocks/SlotMachineClock.h"
#include "clocks/MatrixRainClock.h"
#include "clocks/MegamanClock.h"
#include "clocks/SonicClock.h"
#include "clocks/StreetFighterClock.h"
#include <esp_heap_caps.h>

ClockEngine::ClockEngine() : matrixDisplay(nullptr), activeFace(nullptr), currentTheme(THEME_NONE) {
    currentTime = {10, 42, 00};
}

ClockEngine::ClockEngine(IDrawingSurface* display) : matrixDisplay(display), activeFace(nullptr), currentTheme(THEME_NONE) {
    currentTime = {10, 42, 00};
}

namespace {
template <class T> constexpr size_t maxOf(size_t a) { return a > sizeof(T) ? a : sizeof(T); }

/// Size of the largest clock face object. The arena is sized from this single source of truth, so a new
/// (bigger) face automatically grows the reservation and the declared descriptor requirement.
#if !defined(HARDWARE_PROFILE_ESP32_DEV)
constexpr size_t faceArenaBytes() {
    return maxOf<CyberpunkClock>(maxOf<FlipClock>(maxOf<PongClock>(maxOf<TetrisClock>(maxOf<WordClock>(
           maxOf<BinaryClock>(maxOf<PacmanClock>(maxOf<VersusClock>(maxOf<MatrixRainClock>(maxOf<SlotMachineClock>(
           maxOf<MarioClock>(maxOf<CastleClock>(maxOf<PokedexClock>(maxOf<WorldMapClock>(maxOf<WordsClockFace>(
           maxOf<MegamanClock>(maxOf<SonicClock>(maxOf<MetalSlugClock>(maxOf<StreetFighterClock>(maxOf<ArcadeClock>(0))))))))))))))))))));
}
#else
constexpr size_t faceArenaBytes() {
    return maxOf<CyberpunkClock>(maxOf<FlipClock>(maxOf<PongClock>(maxOf<TetrisClock>(maxOf<WordClock>(
           maxOf<BinaryClock>(maxOf<PacmanClock>(maxOf<VersusClock>(maxOf<MatrixRainClock>(maxOf<SlotMachineClock>(
           maxOf<MarioClock>(maxOf<CastleClock>(maxOf<MegamanClock>(maxOf<SonicClock>(maxOf<StreetFighterClock>(maxOf<ArcadeClock>(0))))))))))))))));
}
#endif
constexpr size_t kFaceArenaBytes = (faceArenaBytes() + 7u) & ~size_t(7);
}

bool ClockEngine::reserveFaceArena() {
    if (_faceArena) return true;
    _faceArena = heap_caps_aligned_alloc(8, kFaceArenaBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!_faceArena) {
        LOGW("ClockEngine", "Face arena (%u B) unavailable (largestBlock=%u), using heap fallback", (unsigned)kFaceArenaBytes,
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    return _faceArena != nullptr;
}

void ClockEngine::releaseFaceArena() {
    if (_faceArena) {
        heap_caps_free(_faceArena);
        _faceArena = nullptr;
    }
}

void ClockEngine::destroyFace() {
    if (!activeFace) return;
    if (_faceInArena) {
        activeFace->~ClockFace();
    } else {
        delete activeFace;
    }
    activeFace = nullptr;
    _faceInArena = false;
}

template <class Face, class... Args>
ClockFace* ClockEngine::makeFace(Args&&... args) {
    static_assert(sizeof(Face) <= kFaceArenaBytes, "Face larger than the face arena");
    // Only large faces (Tetris: ~10 KB block array) justify a persistent arena. Small faces (20-120 B)
    // go to the heap and give the arena back, so a small theme never pins DRAM it does not need.
    constexpr bool kLargeFace = sizeof(Face) > 1024;
    if (kLargeFace && reserveFaceArena()) {
        _faceInArena = true;
        return new (_faceArena) Face(static_cast<Args&&>(args)...);
    }
    if (!kLargeFace) releaseFaceArena();
    _faceInArena = false;
    return new (std::nothrow) Face(static_cast<Args&&>(args)...);
}

ClockEngine::~ClockEngine() {
    destroyFace();
    releaseFaceArena();
}

void ClockEngine::setTheme(PublisherTheme theme, bool forceReload, const EngineConfig* config) {
    if (!forceReload && currentTheme == theme && activeFace != nullptr) {
        return;
    }

    destroyFace();

    currentTheme = theme;

    if (theme == THEME_CYBERPUNK) {
        activeFace = makeFace<CyberpunkClock>(matrixDisplay, config);
    } else if (theme == THEME_FLIP) {
        activeFace = makeFace<FlipClock>(matrixDisplay, config);
    } else if (theme == 22) {
        activeFace = makeFace<PongClock>(matrixDisplay, config);
    } else if (theme == 23) {
        activeFace = makeFace<TetrisClock>(matrixDisplay, false, config); // Normal Tetris
    } else if (theme == 29) {
        activeFace = makeFace<TetrisClock>(matrixDisplay, true, config); // Gameboy Tetris
    } else if (theme == 24) {
        activeFace = makeFace<WordClock>(matrixDisplay, config);
    } else if (theme == 25) {
        activeFace = makeFace<BinaryClock>(matrixDisplay, config);
    } else if (theme == 26) {
        activeFace = makeFace<PacmanClock>(matrixDisplay, config);
    } else if (theme == 27) {
        activeFace = makeFace<VersusClock>(matrixDisplay, config);
    } else if (theme == THEME_MATRIX_RAIN) {
        activeFace = makeFace<MatrixRainClock>(matrixDisplay, config);
    } else if (theme == 28) {
        activeFace = makeFace<SlotMachineClock>(matrixDisplay, config);
    } else if (theme == 30) {
        activeFace = makeFace<MarioClock>(matrixDisplay, config);
    } else if (theme == 31) {
        activeFace = makeFace<CastleClock>(matrixDisplay, config);
    } else if (theme == 34) {
        activeFace = makeFace<PacmanClock>(matrixDisplay, config, true);   // Ms Pac-Man
    } else if (theme == 35 || theme == THEME_MEGAMAN_CLOCK) {
        activeFace = makeFace<MegamanClock>(matrixDisplay, config);
    } else if (theme == 12 || theme == 38 || theme == THEME_STREET_FIGHTER || theme == THEME_RYU) {
        activeFace = makeFace<StreetFighterClock>(matrixDisplay, config);
    } else if (theme == 39 || theme == THEME_SONIC) {
        activeFace = makeFace<SonicClock>(matrixDisplay, config);
#if !defined(HARDWARE_PROFILE_ESP32_DEV)
    } else if (theme == 32) {
        activeFace = makeFace<PokedexClock>(matrixDisplay, config);
    } else if (theme == 33) {
        activeFace = makeFace<WorldMapClock>(matrixDisplay, config);
    } else if (theme == 37) {
        activeFace = makeFace<WordsClockFace>(matrixDisplay, config);
    } else if (theme == 41 || theme == THEME_METAL_SLUG) {
        activeFace = makeFace<MetalSlugClock>(matrixDisplay, config);
#endif
    } else {
        ClockFace* arcade = makeFace<ArcadeClock>(matrixDisplay, config);
        if (arcade) {
            static_cast<ArcadeClock*>(arcade)->setTheme(theme);
            activeFace = arcade;
        }
    }

    if (!activeFace) {
        LOGW("ClockEngine", "Failed to allocate clock theme %d (largestBlock=%u, free=%u), falling back to basic ArcadeClock", (int)theme,
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT), (unsigned)ESP.getFreeHeap());
        ClockFace* fallback = makeFace<ArcadeClock>(matrixDisplay, config);
        if (fallback) {
            static_cast<ArcadeClock*>(fallback)->setTheme(THEME_NONE);
            activeFace = fallback;
        }
    }

    if (activeFace) {
        activeFace->draw(currentTime);
    }
}

void ClockEngine::updateTime(const TimeData& t) {
    currentTime = t;
    if (activeFace) {
        activeFace->draw(t);
    }
}

bool ClockEngine::loop() {
    if (activeFace) {
        activeFace->update();
    }
    return true;
}

void ClockEngine::updateFormatMode(const EngineConfig* config) {
    if (!config) {
        _formatMode = ClockFormatMode::SYSTEM;
        return;
    }
    String fmt = config->getString("clock_format", "system");
    if (fmt.isEmpty() || fmt.equalsIgnoreCase("system")) {
        fmt = config->getString("format", "system");
    }
    if (fmt.indexOf("%I") >= 0) {
        _formatMode = ClockFormatMode::FORCE_12H;
    } else if (fmt.indexOf("%H") >= 0) {
        _formatMode = ClockFormatMode::FORCE_24H;
    } else {
        _formatMode = ClockFormatMode::SYSTEM;
    }
}

// =========================================================
// IEngine Implementation
// =========================================================

EngineError ClockEngine::initialize(EngineContext* context, const EngineConfig* config) {
    matrixDisplay = context ? context->getSurface() : nullptr;
    currentConfig = config;
    updateFormatMode(config);
    int theme = config ? config->getInt("clock_theme", config->getInt("theme", 0)) : 0;
    currentTheme = static_cast<PublisherTheme>(theme);
    return EngineError::OK;
}

bool ClockEngine::needsClear() const {
    return activeFace ? activeFace->wantsClear() : true;
}

bool ClockEngine::hasNewFrame() const {
    return activeFace ? activeFace->hasNewFrame() : true;
}

void ClockEngine::activate() {
    if (!activeFace) {
        setTheme(currentTheme, true, currentConfig);
    }
    if (activeFace) activeFace->onActivated();
    // Clock is active, maybe reset time fetcher
}

void ClockEngine::update(EngineContext* context) {
    if (configDirty) {
        configDirty = false;
        if (currentConfig) {
            updateFormatMode(currentConfig);
            int theme = currentConfig->getInt("clock_theme", currentConfig->getInt("theme", 0));
            setTheme(static_cast<PublisherTheme>(theme), true, currentConfig);
        }
    }
    
    if (context) {
        struct tm timeinfo;
        context->getSystemTime(&timeinfo);
        bool is24h = true;
        if (_formatMode == ClockFormatMode::FORCE_12H) {
            is24h = false;
        } else if (_formatMode == ClockFormatMode::FORCE_24H) {
            is24h = true;
        } else {
            extern ConfigLoader config;
            ConfigSnapshotGuard guard = config.acquireSnapshot();
            is24h = guard.get().system.format24h;
        }
        int h = timeinfo.tm_hour;
        if (!is24h) {
            h = (h % 12 == 0) ? 12 : (h % 12);
        }
        currentTime.hours = h;
        currentTime.minutes = timeinfo.tm_min;
        currentTime.seconds = timeinfo.tm_sec;
    }
    if (activeFace) {
        activeFace->draw(currentTime);
    }
}

void ClockEngine::render(EngineContext* context) {
    if (activeFace) {
        activeFace->update();
    }
}

void ClockEngine::deactivate() {
    if (activeFace) {
        destroyFace();
        LOGI("ClockEngine", "Deallocated active clock face on deactivate.");
    }
    // Release only (Invariant 15): the whole block goes back to the heap for the next engine (e.g. TLS).
    releaseFaceArena();
}

void ClockEngine::onConfigChanged(const EngineConfig* config) {
    if (config) {
        currentConfig = config;
        updateFormatMode(config);
        int theme = config->getInt("clock_theme", config->getInt("theme", 0));
        currentTheme = static_cast<PublisherTheme>(theme);
        if (activeFace) {
            setTheme(currentTheme, true, config);
        } else {
            configDirty = true;
        }
    }
}

void ClockEngine::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    if (activeFace) {
        activeFace->onDisplayGeometryChanged(geometry);
    }
}

EngineDescriptor ClockEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor clockDesc;
    clockDesc.metadata = {"clock", "Clock", "info", FIRMWARE_VERSION};
    clockDesc.capabilities.realtime = true;
    clockDesc.requirements.needsAudio = false;
    clockDesc.requirements.targetFps = 60;
    clockDesc.requirements.prefersDoubleBuffer = true;
    clockDesc.requirements.supportsSingleBuffer = true;
    clockDesc.requirements.internalPersistentBytes = 4000;
    static_assert(kFaceArenaBytes > 0, "Face arena must be sized");
    clockDesc.schema.fields = {
        ConfigField("clock_theme", ConfigType::ENUM, "Clock Theme", "Visual theme / clockface", "0", false, "", "", "", "", "/api/themes", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("clock_format", ConfigType::ENUM, "Time Format", "POSIX strftime format", "system", false, "", "", "", "system:System (General),%H:%M:%S:24 Hours with seconds (%H:%M:%S),%H:%M:24 Hours without seconds (%H:%M),%I:%M:%S %p:12 Hours with seconds (%I:%M:%S %p),%I:%M %p:12 Hours without seconds (%I:%M %p)", "", false, "", ValidationPolicy::Accept),
        ConfigField("clock_font", ConfigType::ENUM, "Font", "Display typeface", "PressStart2P.ttf", false, "", "", "", "", "/api/fonts", false, "clock_theme!=30,31,32,33,35,37,38,39", ValidationPolicy::FallbackDefault),
        ConfigField("timezone", ConfigType::ENUM, "Timezone", "Select timezone or region", "system", false, "", "", "", "system:System (General)", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("clock_size", ConfigType::INTEGER, "Font Size", "Text scaling multiplier", "2", false, "1", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("clock_speed", ConfigType::INTEGER, "Animation Speed", "Animation speed in percent (Tetris block fall, Pac-Man sweep); lower is slower", "100", false, "25", "300", "25", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("clock_color_1", ConfigType::COLOR, "Primary Color", "Custom gradient top color", "#ffffff", false, "", "", "", "", "", false, "clock_theme=20", ValidationPolicy::Accept),
        ConfigField("clock_color_2", ConfigType::COLOR, "Secondary Color", "Custom gradient bottom color", "#ff00ff", false, "", "", "", "", "", false, "clock_theme=20", ValidationPolicy::Accept),
        ConfigField("clock_glow", ConfigType::ENUM, "Glow Outline", "Halo around the digits, the effect the Matrix face uses", "0", false, "", "", "", "0:Off,1:Neon (Matrix style),2:Custom outline color", "", false, "clock_theme!=30,31,32,33,35,38,39", ValidationPolicy::FallbackDefault),
        ConfigField("clock_glow_color", ConfigType::COLOR, "Outline Color", "Color of the outline drawn around the digits", "#00ff41", false, "", "", "", "", "", false, "clock_glow=2", ValidationPolicy::Accept),
        ConfigField("clock_offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("clock_offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    clockDesc.factory = []() { return std::unique_ptr<IEngine>(new ClockEngine()); };
    return clockDesc;
}

