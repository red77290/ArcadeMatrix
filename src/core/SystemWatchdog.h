/**
 * @file SystemWatchdog.h
 * @brief Persistent RTC reset context and diagnostic telemetry for ArcadeMatrix V4.
 */
#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstddef>

#if defined(ESP32)
#include "esp_attr.h"
#include "esp_system.h"
#else
#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif
typedef enum {
    ESP_RST_UNKNOWN = 0,
    ESP_RST_POWERON,
    ESP_RST_EXT,
    ESP_RST_SW,
    ESP_RST_PANIC,
    ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT,
    ESP_RST_WDT,
    ESP_RST_DEEPSLEEP,
    ESP_RST_BROWNOUT,
    ESP_RST_SDIO
} esp_reset_reason_t;
#endif

/**
 * @struct LastResetContext
 * @brief Telemetry snapshot preserved across soft reboots and watchdog events in RTC memory.
 */
struct LastResetContext {
    uint32_t magic = 0;              ///< Signature 0xB007C0DE
    uint16_t version = 1;            ///< Schema version (1)
    uint16_t reserved = 0;           ///< 32-bit alignment padding
    esp_reset_reason_t reason = ESP_RST_UNKNOWN;
    char activeEngineId[16] = {0};
    uint8_t colorDepth = 0;
    size_t freeInternalHeap = 0;
    size_t largestDmaBlock = 0;
    uint32_t core1StallMs = 0;
    bool tlsSessionActive = false;
    uint32_t timestamp = 0;
    uint32_t crc32 = 0;              ///< CRC32 integrity check
};

class SystemWatchdog {
public:
    static SystemWatchdog& instance();

    /**
     * @brief Updates active runtime engine context in RTC memory.
     */
    void recordEngineState(const char* engineId, uint8_t depth, bool tlsActive);

    /**
     * @brief Records Core 1 heartbeat and execution latency.
     */
    void recordCore1Heartbeat(uint32_t frameDurationUs);

    const LastResetContext& getLastResetContext() const { return m_capturedContext; }
    esp_reset_reason_t getCurrentResetReason() const { return m_currentResetReason; }
    bool hadUnexpectedReset() const { return m_hadUnexpectedReset; }

private:
    SystemWatchdog();
    LastResetContext m_capturedContext;
    esp_reset_reason_t m_currentResetReason = ESP_RST_UNKNOWN;
    bool m_hadUnexpectedReset = false;
};
