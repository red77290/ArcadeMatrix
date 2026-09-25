#include "SdSpace.h"
#include "SDUtils.h"
#include "SdLockGuard.h"
#include "Logger.h"
#include "../hal/BoardProfile.h"
#include <Arduino.h>
#include <atomic>

namespace {
    std::atomic<uint64_t> g_total{0};
    std::atomic<uint64_t> g_free{0};
    std::atomic<uint32_t> g_measuredAt{0};
    std::atomic<bool> g_valid{false};
    std::atomic<bool> g_dirty{true};
    TaskHandle_t g_task = nullptr;

    constexpr uint32_t FIRST_DELAY_MS   = 20000;    // let boot, Wi-Fi and the first screens settle
    constexpr uint32_t MIN_INTERVAL_MS  = 60000;    // never re-measure faster than this
    constexpr uint32_t PERIODIC_MS      = 30UL * 60UL * 1000UL;

    bool measure(uint64_t& total, uint64_t& freeB) {
        if (!BoardProfile::current().isStorageAvailable()) return false;
        SdLockGuard guard(pdMS_TO_TICKS(15000));
        if (!guard) return false;
        total = BoardProfile::current().getStorageTotalBytes();
        freeB = BoardProfile::current().getStorageFreeBytes();
        return total > 0;
    }

    void taskFn(void*) {
        vTaskDelay(pdMS_TO_TICKS(FIRST_DELAY_MS));
        for (;;) {
            uint32_t now = millis();
            bool due = g_dirty.load() && (!g_valid.load() || (now - g_measuredAt.load()) >= MIN_INTERVAL_MS);
            if (!due && g_valid.load() && (now - g_measuredAt.load()) >= PERIODIC_MS) due = true;
            if (due) {
                uint64_t total = 0, freeB = 0;
                uint32_t t0 = millis();
                if (measure(total, freeB)) {
                    g_total.store(total);
                    g_free.store(freeB);
                    g_measuredAt.store(millis());
                    g_valid.store(true);
                    g_dirty.store(false);
                    LOGI("SdSpace", "SD card: %.2f GB free of %.2f GB (%lu ms)",
                         freeB / 1073741824.0, total / 1073741824.0, (unsigned long)(millis() - t0));
                } else {
                    LOGW("SdSpace", "SD free-space measurement skipped (card busy or unsupported); retrying later");
                    vTaskDelay(pdMS_TO_TICKS(30000));
                    continue;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}

namespace SdSpace {
    void start() {
        if (g_task) return;
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
        constexpr size_t stackSize = 6144;
#else
        constexpr size_t stackSize = 3072;
#endif
        if (xTaskCreatePinnedToCore(taskFn, "sd_space", stackSize, nullptr, 1, &g_task, 0) != pdPASS) {
            LOGW("SdSpace", "Could not start the SD free-space task");
            g_task = nullptr;
        }
    }
    void requestRefresh() { g_dirty.store(true); }
    bool get(uint64_t& totalBytes, uint64_t& freeBytes, uint32_t& ageMs) {
        if (!g_valid.load()) return false;
        totalBytes = g_total.load();
        freeBytes = g_free.load();
        ageMs = millis() - g_measuredAt.load();
        return true;
    }
}
