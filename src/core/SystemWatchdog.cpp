#include "SystemWatchdog.h"
#include <cstring>

#if defined(ESP32)
#include "esp_heap_caps.h"
#endif

static constexpr uint32_t RESET_MAGIC = 0xB007C0DE;
static constexpr uint16_t RESET_VERSION = 1;

#if defined(ESP32)
RTC_NOINIT_ATTR static LastResetContext s_rtcResetContext;
#else
static LastResetContext s_rtcResetContext;
#endif

static uint32_t calculateResetContextCrc(const LastResetContext& ctx) {
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&ctx);
    size_t len = offsetof(LastResetContext, crc32);
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

SystemWatchdog& SystemWatchdog::instance() {
    static SystemWatchdog s_instance;
    return s_instance;
}

SystemWatchdog::SystemWatchdog() {
#if defined(ESP32)
    m_currentResetReason = esp_reset_reason();
#else
    m_currentResetReason = ESP_RST_POWERON;
#endif

    m_hadUnexpectedReset = (m_currentResetReason != ESP_RST_POWERON && m_currentResetReason != ESP_RST_SW);

    // Validate previous boot context integrity via magic, version and CRC32
    if (s_rtcResetContext.magic == RESET_MAGIC && s_rtcResetContext.version == RESET_VERSION) {
        uint32_t expectedCrc = calculateResetContextCrc(s_rtcResetContext);
        if (expectedCrc == s_rtcResetContext.crc32) {
            m_capturedContext = s_rtcResetContext; // Preserves pre-reset reason intact
        } else {
            memset(&m_capturedContext, 0, sizeof(m_capturedContext));
        }
    } else {
        memset(&m_capturedContext, 0, sizeof(m_capturedContext));
    }

    // Arm clean context for current boot cycle
    s_rtcResetContext.magic = RESET_MAGIC;
    s_rtcResetContext.version = RESET_VERSION;
    s_rtcResetContext.reserved = 0;
    s_rtcResetContext.reason = m_currentResetReason;
    s_rtcResetContext.activeEngineId[0] = '\0';
    s_rtcResetContext.colorDepth = 0;
    s_rtcResetContext.freeInternalHeap = 0;
    s_rtcResetContext.largestDmaBlock = 0;
    s_rtcResetContext.core1StallMs = 0;
    s_rtcResetContext.tlsSessionActive = false;
    s_rtcResetContext.timestamp = millis();
    s_rtcResetContext.crc32 = calculateResetContextCrc(s_rtcResetContext);
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
    s_rtcResetContext.crc32 = calculateResetContextCrc(s_rtcResetContext);
}

void SystemWatchdog::recordCore1Heartbeat(uint32_t frameDurationUs) {
    s_rtcResetContext.core1StallMs = frameDurationUs / 1000;
    s_rtcResetContext.timestamp = millis();
    s_rtcResetContext.crc32 = calculateResetContextCrc(s_rtcResetContext);
}
