#pragma once

#include "../core/types.h"
#include "../rendering/renderer_manager.h"
#include "zombie_auditor.h"
#include "gpu_profiler.h"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>
#include <functional>
#include <cmath>

namespace morphic {

// Phase 2A.4 — Per-Thread Activity Attribution.
//
// Uses ONLY public Win32 APIs (Toolhelp32, GetThreadTimes, QueryThreadCycleTime).
// NO NT-level queries, NO ETW, NO undocumented APIs.
//
// What it measures per thread:
//   1. CPU time delta (kernel + user) across observation intervals
//   2. Cycle count delta (QueryThreadCycleTime — finer than GetThreadTimes)
//   3. Thread priority (scheduling hints)
//   4. Thread lifetime (creation time → age)
//   5. Wake cadence classification (idle/sporadic/periodic/active)
//   6. Active-to-hidden delta ratio
//
// Attribution strategy:
//   - Enumerate all threads via Toolhelp32
//   - Group by creation time to correlate with engine instantiations
//   - Track CPU deltas over intervals to classify behavior
//   - A thread with 0 CPU delta across 30s = truly dormant
//   - A thread with periodic CPU delta = operationally alive
class ThreadActivityAuditor {
public:

    // Cadence classification
    enum class WakeCadence {
        Dormant,       // 0 CPU delta across observation window
        Sporadic,      // < 5 wakes/sec inferred
        Periodic,      // Regular cadence detected (timer-like)
        Active,        // Continuous CPU consumption
        Unknown
    };

    static const char* cadenceName(WakeCadence c) {
        switch (c) {
            case WakeCadence::Dormant: return "Dormant";
            case WakeCadence::Sporadic: return "Sporadic";
            case WakeCadence::Periodic: return "Periodic";
            case WakeCadence::Active: return "Active";
            default: return "Unknown";
        }
    }

    // Single thread snapshot
    struct ThreadSnapshot {
        DWORD threadId = 0;
        int priority = 0;

        // CPU times
        FILETIME creationTime = {};
        FILETIME kernelTime = {};
        FILETIME userTime = {};
        uint64_t cycleCount = 0;

        // Derived
        double ageMs = 0.0;           // Since thread creation
        double kernelMs = 0.0;        // Total kernel CPU
        double userMs = 0.0;          // Total user CPU
        double totalCpuMs = 0.0;
    };

    // Thread activity record across observation window
    struct ThreadActivity {
        DWORD threadId = 0;
        int priority = 0;
        double ageMs = 0.0;

        // Deltas across observation window
        double kernelDeltaMs = 0.0;
        double userDeltaMs = 0.0;
        double totalDeltaMs = 0.0;
        uint64_t cycleDelta = 0;

        // Classification
        WakeCadence cadence = WakeCadence::Unknown;
        double estimatedWakesPerSec = 0.0;

        // Grouping heuristic
        int engineGroup = -1;   // -1 = unattributed, 0 = main, 1+ = engine N
    };

    // Full audit result
    struct AuditResult {
        double observationWindowMs = 0.0;
        int totalThreads = 0;

        // Thread classifications
        int dormantCount = 0;
        int sporadicCount = 0;
        int periodicCount = 0;
        int activeCount = 0;

        // Per-thread details
        std::vector<ThreadActivity> activities;

        // Summary
        double totalCpuDeltaMs = 0.0;
        double activeCpuDeltaMs = 0.0;     // CPU consumed by non-dormant threads
        int threadsCreatedAfterBaseline = 0; // Engine-correlated threads

        std::string details;
    };

    ThreadActivityAuditor() = default;

    // Snapshot all threads in the current process
    std::vector<ThreadSnapshot> captureThreadSnapshot() const {
        std::vector<ThreadSnapshot> threads;
        DWORD pid = GetCurrentProcessId();

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return threads;

        THREADENTRY32 te;
        te.dwSize = sizeof(te);

        auto now = std::chrono::high_resolution_clock::now();
        auto nowFt = chronoToFiletime(now);

        if (Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID != pid) continue;

                HANDLE hThread = OpenThread(
                    THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION,
                    FALSE, te.th32ThreadID);
                if (!hThread) continue;

                ThreadSnapshot ts;
                ts.threadId = te.th32ThreadID;
                ts.priority = GetThreadPriority(hThread);

                FILETIME exitTime;
                if (GetThreadTimes(hThread, &ts.creationTime, &exitTime,
                    &ts.kernelTime, &ts.userTime)) {
                    ts.kernelMs = filetimeToMs(ts.kernelTime);
                    ts.userMs = filetimeToMs(ts.userTime);
                    ts.totalCpuMs = ts.kernelMs + ts.userMs;
                    ts.ageMs = filetimeDeltaMs(ts.creationTime, nowFt);
                }

                // Cycle count (finer granularity than GetThreadTimes)
                ULONG64 cycles = 0;
                if (QueryThreadCycleTime(hThread, &cycles)) {
                    ts.cycleCount = cycles;
                }

                CloseHandle(hThread);
                threads.push_back(ts);
            } while (Thread32Next(snap, &te));
        }

        CloseHandle(snap);
        return threads;
    }

    // Run full attribution audit
    // Captures before/after snapshots separated by observationMs
    AuditResult runAudit(int observationMs = 5000) const {
        AuditResult result;

        // Before snapshot
        auto before = captureThreadSnapshot();
        auto beforeTime = std::chrono::high_resolution_clock::now();

        // Wait — pump messages to keep system responsive
        DWORD remaining = static_cast<DWORD>(observationMs);
        auto start = GetTickCount64();
        while ((GetTickCount64() - start) < remaining) {
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            Sleep(50);  // Small sleep to avoid busy-wait
        }

        // After snapshot
        auto after = captureThreadSnapshot();
        auto afterTime = std::chrono::high_resolution_clock::now();

        result.observationWindowMs = std::chrono::duration<double, std::milli>(
            afterTime - beforeTime).count();
        result.totalThreads = static_cast<int>(after.size());

        // Build lookup for before snapshot
        std::unordered_map<DWORD, ThreadSnapshot> beforeMap;
        for (const auto& ts : before) {
            beforeMap[ts.threadId] = ts;
        }

        // Compute deltas and classify each thread
        for (const auto& afterTs : after) {
            ThreadActivity ta;
            ta.threadId = afterTs.threadId;
            ta.priority = afterTs.priority;
            ta.ageMs = afterTs.ageMs;

            auto it = beforeMap.find(afterTs.threadId);
            if (it != beforeMap.end()) {
                const auto& beforeTs = it->second;

                ta.kernelDeltaMs = afterTs.kernelMs - beforeTs.kernelMs;
                ta.userDeltaMs = afterTs.userMs - beforeTs.userMs;
                ta.totalDeltaMs = ta.kernelDeltaMs + ta.userDeltaMs;
                ta.cycleDelta = afterTs.cycleCount - beforeTs.cycleCount;
            } else {
                // Thread created during observation — new thread
                ta.totalDeltaMs = afterTs.totalCpuMs;
                ta.cycleDelta = afterTs.cycleCount;
                result.threadsCreatedAfterBaseline++;
            }

            // Classify cadence based on CPU delta during observation window
            double windowSec = result.observationWindowMs / 1000.0;
            if (windowSec > 0) {
                // Estimate wake frequency from CPU time ratio
                // Assume each wake consumes ~0.1ms of CPU time (heuristic)
                if (ta.totalDeltaMs < 0.01) {
                    ta.cadence = WakeCadence::Dormant;
                    ta.estimatedWakesPerSec = 0.0;
                } else if (ta.totalDeltaMs < 5.0) {
                    ta.cadence = WakeCadence::Sporadic;
                    ta.estimatedWakesPerSec = ta.totalDeltaMs / (0.1 * windowSec);
                } else if (ta.totalDeltaMs < windowSec * 100) {
                    // Less than 10% of wall time in CPU — periodic
                    ta.cadence = WakeCadence::Periodic;
                    ta.estimatedWakesPerSec = ta.totalDeltaMs / (0.1 * windowSec);
                } else {
                    ta.cadence = WakeCadence::Active;
                    ta.estimatedWakesPerSec = ta.totalDeltaMs / (0.1 * windowSec);
                }
            }

            // Count by cadence
            switch (ta.cadence) {
                case WakeCadence::Dormant: result.dormantCount++; break;
                case WakeCadence::Sporadic: result.sporadicCount++; break;
                case WakeCadence::Periodic: result.periodicCount++; break;
                case WakeCadence::Active: result.activeCount++; break;
                default: break;
            }

            result.totalCpuDeltaMs += ta.totalDeltaMs;
            if (ta.cadence != WakeCadence::Dormant) {
                result.activeCpuDeltaMs += ta.totalDeltaMs;
            }

            result.activities.push_back(ta);
        }

        // Sort by CPU delta descending (hottest threads first)
        std::sort(result.activities.begin(), result.activities.end(),
            [](const ThreadActivity& a, const ThreadActivity& b) {
                return a.totalDeltaMs > b.totalDeltaMs;
            });

        // Build details
        result.details = "Threads=" + std::to_string(result.totalThreads) +
            " Window=" + std::to_string(result.observationWindowMs) + "ms" +
            " Dormant=" + std::to_string(result.dormantCount) +
            " Sporadic=" + std::to_string(result.sporadicCount) +
            " Periodic=" + std::to_string(result.periodicCount) +
            " Active=" + std::to_string(result.activeCount) +
            " TotalCPU=" + std::to_string(result.totalCpuDeltaMs) + "ms" +
            " ActiveCPU=" + std::to_string(result.activeCpuDeltaMs) + "ms";

        // Top 5 hottest threads
        int top = (std::min)(5, static_cast<int>(result.activities.size()));
        for (int i = 0; i < top; i++) {
            const auto& ta = result.activities[i];
            if (ta.totalDeltaMs < 0.01) break;
            result.details += " T" + std::to_string(ta.threadId) +
                "=" + std::to_string(ta.totalDeltaMs) + "ms(" +
                cadenceName(ta.cadence) + ")";
        }

        OutputDebugStringA(("THREAD_AUDIT: " + result.details + "\n").c_str());

        return result;
    }

private:
    static double filetimeToMs(const FILETIME& ft) {
        ULARGE_INTEGER li;
        li.LowPart = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;
        return static_cast<double>(li.QuadPart) / 10000.0;
    }

    static double filetimeDeltaMs(const FILETIME& a, const FILETIME& b) {
        return filetimeToMs(b) - filetimeToMs(a);
    }

    static FILETIME chronoToFiletime(
        std::chrono::high_resolution_clock::time_point tp)
    {
        // Approximate: use current system time
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        return ft;
    }
};

}  // namespace morphic
