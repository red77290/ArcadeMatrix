#pragma once

#include <Arduino.h>
#include "../../include/core/EngineContract.h"
#include "AgendaIcsParser.h"

/**
 * @class AgendaEngine
 * @brief Zero-RAM persistent iCal (.ics) connected calendar engine with arcade pixel-art sprites.
 *
 * Enforces all Golden Rules:
 * - static_assert(sizeof(AgendaEngine) <= 1600)
 * - Zero heap allocation, zero String, zero std::vector, zero mutex, zero SD I/O in update()/render().
 * - Persistent binary cache on MicroSD (/cache/agenda_<id>.bin) surviving reboots.
 * - O(1) needsTlsFetch() and synchronous prefetchData() in HUB75 OE=HIGH blanking window on esp32dev.
 */
class AgendaEngine : public IEngine {
public:
    static constexpr size_t MAX_ACTIVE_EVENTS = 7U;

    enum class ScrollPhase : uint8_t {
        PauseStart = 0,
        Scrolling  = 1,
        PauseEnd   = 2
    };

    AgendaEngine();
    ~AgendaEngine() override;

    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    bool shutdownForDestruction() override;
    void onConfigChanged(const EngineConfig* config) override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;
    void resume() override { requestRedraw(); }

    void prefetchData() override;
    bool needsTlsFetch() const override;

    bool isFinished() const override { return m_finished; }
    bool isRealtime() const override { return m_isRealtimeNow; }
    bool hasNewFrame() const override { return m_renderedNewFrame; }
    bool needsClear() const override { return false; }

    // deterministic unit-test hooks (zero heap)
    void setTestNowEpoch(uint32_t epoch) { m_testNowEpoch = epoch; }
    uint32_t lastFetchEpoch() const { return m_lastFetchEpoch; }
    uint32_t configCrc32() const { return m_configCrc32; }
    bool hasValidSdCache() const { return m_hasValidSdCache; }
    uint8_t activeEventCount() const { return m_activeCount; }
    const AgendaEventPod& activeEvent(uint8_t idx) const {
        return m_activeEvents[idx < MAX_ACTIVE_EVENTS ? idx : 0];
    }
    void injectEventsForTest(const AgendaEventPod* events, uint8_t count, uint32_t fetchEpoch);
    bool serializeCacheToBuffer(uint8_t* outBuf, size_t maxLen, const AgendaEventPod* events, uint8_t count, uint32_t fetchEpoch, size_t& outWritten) const;
    bool loadHeaderFromBuffer(const uint8_t* buf, size_t len);
    bool loadActiveEventsFromBuffer(const uint8_t* buf, size_t len, uint32_t nowEpoch);

private:
    DisplayGeometry m_geometry{};

    // Fixed POD configuration (Zero String on heap)
    char m_instanceId[24] = "agenda_main";
    char m_icsUrl1[224] = "";
    char m_icsUrl2[224] = "";
    char m_icsLabel1[20] = "Perso";
    char m_icsLabel2[20] = "Pro";
    uint16_t m_icsColor1 = 0x073F; // #00E5FF Cyan Arcade
    uint16_t m_icsColor2 = 0xFD80; // #FFB300 Amber
    uint16_t m_cacheTtlMin = 60;
    uint8_t m_horizonDays = 7;
    uint8_t m_eventsShown = 3;
    uint8_t m_imminentMin = 15;
    bool m_emptyChill = true;
    bool m_funSprites = true;

    // Cache & fetch tracking
    uint32_t m_configCrc32 = 0;
    uint32_t m_lastFetchEpoch = 0;
    uint32_t m_testNowEpoch = 0;
    int32_t m_localTzOffsetSec = 3600;
    bool m_hasValidSdCache = false;

    // Active rotation slot state (672 bytes fixed POD)
    AgendaEventPod m_activeEvents[MAX_ACTIVE_EVENTS]{};
    uint8_t m_activeCount = 0;
    uint8_t m_currentSlideIdx = 0;
    uint8_t m_slidesShownCycle = 0;
    uint8_t m_redrawFrames = 2;
    ScrollPhase m_scrollPhase = ScrollPhase::PauseStart;
    int16_t m_scrollOffsetPx = 0;
    int16_t m_maxScrollPx = 0;
    uint32_t m_phaseStartMs = 0;
    uint32_t m_lastSlideMs = 0;
    uint32_t m_lastAnimTickMs = 0;
    uint32_t m_animTick = 0;
    bool m_renderedNewFrame = false;
    bool m_isRealtimeNow = false;
    bool m_finished = false;

    void applyConfig(const EngineConfig* config);
    uint32_t currentEpoch() const;
    void updateLocalTzOffset();
    void buildCacheFilePath(char* outPath, size_t outSize, bool tempFile = false) const;
    bool probeSdCacheHeaderOnly();
    bool loadUpcomingFromSdCache(uint32_t nowEpoch);
    bool writeEventsToSdCache(const AgendaEventPod* events, uint8_t count, uint32_t fetchEpoch);
    void recomputeSlideScrollBounds(uint16_t panelW);
    void requestRedraw() { m_redrawFrames = 2; }

    void renderChillScene(IDrawingSurface* surface, uint16_t w, uint16_t h) const;
    void renderWideCompact(IDrawingSurface* surface, uint16_t w, uint16_t h, uint32_t nowEpoch) const;
    void renderWideHd(IDrawingSurface* surface, uint16_t w, uint16_t h, uint32_t nowEpoch) const;
    void renderSquare(IDrawingSurface* surface, uint16_t w, uint16_t h, uint32_t nowEpoch) const;
    void renderTateQuestLog(IDrawingSurface* surface, uint16_t w, uint16_t h, uint32_t nowEpoch) const;

    static uint16_t parseHexColor565(const char* hex, uint16_t fallback);
    static void formatEventTimePill(uint32_t startEpoch, bool allDay, int32_t tzOffsetSec, uint32_t nowEpoch, char* out, size_t outSize);
};

static_assert(sizeof(AgendaEngine) <= 1600, "AgendaEngine must strictly respect the <= 1600 bytes Lazy-Once RAM budget");

class AgendaEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};
