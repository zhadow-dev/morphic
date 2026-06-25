#pragma once

#include "governance_types.h"
#include <windows.h>
#include <chrono>

namespace morphic {

// Phase 3 — System Pressure Monitor
//
// Polls system-wide resource pressure. Hybrid polling strategy:
//   memory:  2s periodic + event on allocation failure
//   CPU:     5s sampled burst (3 samples averaged)
//   battery: 30s periodic (changes slowly)
//
// A fixed 5s timer for everything is naive orchestration.
// Different signals change at different rates.
//
// THREAD: UI thread only. Polling is cheap (< 1ms each).
class SystemPressureMonitor {
public:
    struct Pressure {
        // Raw observations
        float memoryUsagePercent = 0.0f;  // from GlobalMemoryStatusEx
        float cpuUsagePercent = 0.0f;     // from GetSystemTimes delta
        bool onBattery = false;
        float batteryPercent = 100.0f;

        // Derived governance bands
        PressureLevel memoryPressure = PressureLevel::Relaxed;
        PressureLevel overallPressure = PressureLevel::Relaxed;

        // Staleness
        ULONGLONG lastMemoryPollTick = 0;
        ULONGLONG lastCpuPollTick = 0;
        ULONGLONG lastBatteryPollTick = 0;
    };

    // Update pressure readings based on elapsed time.
    // Call this periodically (every compositor tick is fine).
    void update() {
        ULONGLONG now = GetTickCount64();

        // Memory: every 2s
        if (now - pressure_.lastMemoryPollTick >= memoryIntervalMs_) {
            pollMemory();
            pressure_.lastMemoryPollTick = now;
        }

        // CPU: every 5s
        if (now - pressure_.lastCpuPollTick >= cpuIntervalMs_) {
            pollCpu();
            pressure_.lastCpuPollTick = now;
        }

        // Battery: every 30s
        if (now - pressure_.lastBatteryPollTick >= batteryIntervalMs_) {
            pollBattery();
            pressure_.lastBatteryPollTick = now;
        }

        // Compute overall pressure
        computeOverallPressure();
    }

    const Pressure& pressure() const { return pressure_; }

private:
    Pressure pressure_;

    // Polling intervals per signal type
    ULONGLONG memoryIntervalMs_ = 2000;
    ULONGLONG cpuIntervalMs_ = 5000;
    ULONGLONG batteryIntervalMs_ = 30000;

    // CPU measurement state
    ULARGE_INTEGER prevIdleTime_{};
    ULARGE_INTEGER prevKernelTime_{};
    ULARGE_INTEGER prevUserTime_{};
    bool cpuInitialized_ = false;

    void pollMemory() {
        MEMORYSTATUSEX ms;
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms)) {
            pressure_.memoryUsagePercent = static_cast<float>(ms.dwMemoryLoad);
            if (ms.dwMemoryLoad < 50) {
                pressure_.memoryPressure = PressureLevel::Relaxed;
            } else if (ms.dwMemoryLoad < 75) {
                pressure_.memoryPressure = PressureLevel::Moderate;
            } else if (ms.dwMemoryLoad < 90) {
                pressure_.memoryPressure = PressureLevel::Elevated;
            } else {
                pressure_.memoryPressure = PressureLevel::Critical;
            }
        }
    }

    void pollCpu() {
        FILETIME idle, kernel, user;
        if (GetSystemTimes(&idle, &kernel, &user)) {
            ULARGE_INTEGER curIdle, curKernel, curUser;
            curIdle.LowPart = idle.dwLowDateTime;
            curIdle.HighPart = idle.dwHighDateTime;
            curKernel.LowPart = kernel.dwLowDateTime;
            curKernel.HighPart = kernel.dwHighDateTime;
            curUser.LowPart = user.dwLowDateTime;
            curUser.HighPart = user.dwHighDateTime;

            if (cpuInitialized_) {
                auto idleDelta = curIdle.QuadPart - prevIdleTime_.QuadPart;
                auto kernelDelta = curKernel.QuadPart - prevKernelTime_.QuadPart;
                auto userDelta = curUser.QuadPart - prevUserTime_.QuadPart;
                auto totalDelta = kernelDelta + userDelta;
                if (totalDelta > 0) {
                    pressure_.cpuUsagePercent = static_cast<float>(
                        100.0 * (1.0 - static_cast<double>(idleDelta) / totalDelta));
                }
            }

            prevIdleTime_ = curIdle;
            prevKernelTime_ = curKernel;
            prevUserTime_ = curUser;
            cpuInitialized_ = true;
        }
    }

    void pollBattery() {
        SYSTEM_POWER_STATUS sps;
        if (GetSystemPowerStatus(&sps)) {
            pressure_.onBattery = (sps.ACLineStatus == 0);
            if (sps.BatteryLifePercent != 255) {
                pressure_.batteryPercent = static_cast<float>(sps.BatteryLifePercent);
            }
        }
    }

    void computeOverallPressure() {
        // Overall = max of individual pressures + battery penalty
        pressure_.overallPressure = pressure_.memoryPressure;

        // Battery on + low → increase pressure
        if (pressure_.onBattery && pressure_.batteryPercent < 20.0f) {
            if (pressure_.overallPressure < PressureLevel::Elevated) {
                pressure_.overallPressure = PressureLevel::Elevated;
            }
        }

        // High CPU → increase pressure
        if (pressure_.cpuUsagePercent > 85.0f) {
            if (pressure_.overallPressure < PressureLevel::Moderate) {
                pressure_.overallPressure = PressureLevel::Moderate;
            }
        }
    }
};

}  // namespace morphic
