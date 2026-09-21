#include "CpuLoad.h"
#include <Arduino.h>
#include <esp_freertos_hooks.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
    constexpr int CORES = 2;
    // Everything the hooks touch is integer: they run inside the tick interrupt, where the FPU is
    // not available (a floating-point operation there is an illegal instruction and reboots the board).
    volatile uint32_t s_ticks[CORES] = {0, 0};
    volatile uint32_t s_idleTicks[CORES] = {0, 0};
    volatile uint32_t s_busyPermille[CORES] = {0, 0};   ///< last completed second, 0..1000
    bool s_started = false;

    inline void IRAM_ATTR sample(int cpu) {
        bool idle = (xTaskGetCurrentTaskHandleForCPU(cpu) == xTaskGetIdleTaskHandleForCPU(cpu));
        uint32_t t = s_ticks[cpu] + 1;
        uint32_t i = s_idleTicks[cpu] + (idle ? 1u : 0u);
        if (t >= (uint32_t)configTICK_RATE_HZ) {           // one second at the default 1 kHz tick
            s_busyPermille[cpu] = (t - i) * 1000u / t;
            t = 0; i = 0;
        }
        s_ticks[cpu] = t;
        s_idleTicks[cpu] = i;
    }
    void IRAM_ATTR tick0() { sample(0); }
    void IRAM_ATTR tick1() { sample(1); }
}

namespace CpuLoad {
    void start() {
        if (s_started) return;
        s_started = true;
        esp_register_freertos_tick_hook_for_cpu(tick0, 0);
#if !CONFIG_FREERTOS_UNICORE
        esp_register_freertos_tick_hook_for_cpu(tick1, 1);
#endif
    }
    float core(int cpu) {
        if (cpu < 0 || cpu >= CORES) return 0.0f;
        return s_busyPermille[cpu] / 10.0f;
    }
    float total() {
#if CONFIG_FREERTOS_UNICORE
        return core(0);
#else
        return (core(0) + core(1)) * 0.5f;
#endif
    }
}
