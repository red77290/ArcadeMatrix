#include "SystemWatchdog.h"
#include <cstring>

#if defined(ESP32)
#include "esp_heap_caps.h"
#endif

static constexpr uint32_t RESET_MAGIC = 0xB007C0DE;

#if defined(ESP32)
RTC_NOINIT_ATTR static LastResetContext s_rtcResetContext;
#else
static LastResetContext s_rtcResetContext;
#endif

SystemWatchdog& SystemWatchdog::instance() {
    static SystemWatchdog s_instance;
    return s_instance;
}

SystemWatchdog::SystemWatchdog() {
#if defined(ESP32)
    esp_reset_reason_t reason = esp_reset_reason();
#else
    esp_reset_reason_t reason = ESP_RST_POWERON;
#endif

    if (s_rtcResetContext.magic == RESET_MAGIC) {
        m_capturedContext = s_rtcResetContext;
        m_capturedContext.reason = reason;
        m_hadUnexpectedReset = (reason != ESP_RST_POWERON && reason != ESP_RST_SW);
    }

    // Arm clean context for current boot cycle
    s_rtcResetContext.magic = RESET_MAGIC;
    s_rtcResetContext.reason = reason;
    s_rtcResetContext.activeEngineId[0] = '\0';
    s_rtcResetContext.colorDepth = 0;
    s_rtcResetContext.freeInternalHeap = 0;
    s_rtcResetContext.largestDmaBlock = 0;
    s_rtcResetContext.core1StallMs = 0;
    s_rtcResetContext.tlsSessionActive = false;
    s_rtcResetContext.timestamp = millis();
}

void SystemWatchdog::recordEngineState(const char* engineId, uint8_t depth, bool tlsActive) {
    if (engineId) {
        strncpy(s_rtcResetContext.activeEngineId, engineId, sizeof(s_rtcResetContext.activeEngineId) - 1);
        s_rtcResetContext.activeEngineId[sizeof(s_rtcResetContext.activeEngineId) - 1] = '\0';
    }
    s_rtcResetContext.colorDepth = depth;
    s_rtcResetContext.tlsSessionActive = tlsActive;
#if defined(ESP32)
    s_rtcResetContext.freeInternalHeap = esp_get_free_internal_heap_size();
    s_rtcResetContext.largestDmaBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
#endif
    s_rtcResetContext.timestamp = millis();
}

void SystemWatchdog::recordCore1Heartbeat(uint32_t frameDurationUs) {
    s_rtcResetContext.core1StallMs = frameDurationUs / 1000;
    s_rtcResetContext.timestamp = millis();
}
