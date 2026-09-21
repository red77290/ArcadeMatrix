#pragma once

/**
 * @brief Real CPU load per core, from the FreeRTOS tick hook.
 *
 * The framework is built without run-time statistics, so there is no per-task CPU accounting to
 * read. Instead a tick hook on each core samples, once per millisecond, whether that core is running
 * its idle task; the share of non-idle ticks over the last second is the load. No spinning, no power
 * cost, and it is exactly the figure the RPi build reports from sysinfo, so the dashboards can share
 * one meaning of "CPU Load".
 */
namespace CpuLoad {
    /// Register the tick hooks; call once at boot.
    void start();
    /// Load of one core over the last completed second, 0..100.
    float core(int cpu);
    /// Average of both cores, 0..100.
    float total();
}
