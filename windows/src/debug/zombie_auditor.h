#pragma once

#include "../core/types.h"
#include "../rendering/renderer_manager.h"
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>

namespace morphic {

// Phase 2A.3 — Zombie Engine Behavioral Auditor.
//
// Non-invasive observation of hidden/zombie Flutter engine activity.
// Determines whether zombified engines are truly dormant or operationally active.
//
// What it measures per zombie:
//   1. Thread count delta (before/after zombification)
//   2. Handle count delta (GDI objects, kernel handles)
//   3. CPU wake frequency (is the zombie's thread still waking?)
//   4. Message traffic (does the zombie HWND still process messages?)
//   5. Timer activity (are zombie timers still firing?)
//   6. Idle decay behavior (activity change over time windows)
//
// Design principles:
//   - OBSERVE ONLY. Never mutate zombie state.
//   - Timestamp EVERYTHING. Temporal behavior matters.
//   - Track decay curves, not just point-in-time snapshots.
//
// Usage:
//   ZombieAuditor auditor;
//   auto baseline = auditor.captureProcessSnapshot();
//   // ... zombify engines ...
//   auto postZombie = auditor.captureProcessSnapshot();
//   auto delta = auditor.computeDelta(baseline, postZombie);
//
//   // Idle decay tracking:
//   auditor.beginDecayTracking(rendererManager);
//   // ... wait ...
//   auto decay = auditor.captureDecayPoint(rendererManager);
class ZombieAuditor {
public:

    // Process-wide resource snapshot.
    struct ProcessSnapshot {
        std::chrono::high_resolution_clock::time_point timestamp;
        DWORD processId = 0;

        // Thread info
        int threadCount = 0;

        // Handle counts
        DWORD handleCount = 0;
        DWORD gdiObjects = 0;
        DWORD userObjects = 0;

        // Memory
        SIZE_T workingSetBytes = 0;
        SIZE_T privateBytes = 0;
        SIZE_T pagefileUsage = 0;

        // CPU
        FILETIME kernelTime = {};
        FILETIME userTime = {};
    };

    // Per-zombie HWND snapshot.
    struct ZombieHwndSnapshot {
        RenderId rendererId = 0;
        HWND hwnd = nullptr;
        bool isVisible = false;
        bool isEnabled = false;
        bool hasParent = false;
        DWORD threadId = 0;

        // Message queue state
        int pendingMessages = 0;   // Messages in queue at capture time

        // Timer state
        int timerCount = 0;        // Active timers on this HWND
    };

    // Per-zombie behavioral snapshot.
    struct ZombieBehavior {
        RenderId rendererId = 0;
        NodeId lastSurfaceId = 0;
        RendererLifecycle lifecycle;
        RendererAttachment attachment;

        // Timing
        double ageMs = 0.0;                    // Time since zombification
        double timeSinceCreationMs = 0.0;      // Time since engine was created

        // HWND state
        ZombieHwndSnapshot hwndState;

        // Frame activity (has the engine produced frames since zombification?)
        int64_t totalFramesAtZombify = 0;      // Frames at zombification time
        int64_t totalFramesNow = 0;            // Frames now
        int64_t framesSinceZombify = 0;        // Delta

        // Thread activity for the HWND's thread
        FILETIME threadKernelTime = {};
        FILETIME threadUserTime = {};
    };

    // Resource delta between two process snapshots.
    struct ProcessDelta {
        double elapsedMs = 0.0;

        int threadDelta = 0;
        int handleDelta = 0;
        int gdiDelta = 0;
        int userObjectDelta = 0;

        int64_t workingSetDeltaKB = 0;
        int64_t privateDeltaKB = 0;

        double cpuKernelDeltaMs = 0.0;
        double cpuUserDeltaMs = 0.0;
        double cpuTotalDeltaMs = 0.0;
    };

    // Idle decay point — captured at intervals after zombification.
    struct DecayPoint {
        double elapsedSinceZombifyMs = 0.0;
        ProcessSnapshot processState;
        std::vector<ZombieBehavior> zombieStates;

        // Derived: aggregate zombie activity
        int64_t totalZombieFramesSinceZombify = 0;
        int totalZombieTimers = 0;
        int totalZombiePendingMessages = 0;
    };

    // Full audit result.
    struct AuditResult {
        bool passed = true;
        std::string scenario;

        ProcessSnapshot baselineSnapshot;
        ProcessSnapshot postZombifySnapshot;
        ProcessDelta resourceDelta;

        std::vector<ZombieBehavior> zombieBehaviors;
        std::vector<DecayPoint> decayCurve;

        // Summary
        int zombieCount = 0;
        int activeTimerCount = 0;          // Total timers across all zombies
        int64_t totalFramesSinceZombify = 0;  // Total frames produced by zombies after death
        bool anyZombieRendering = false;    // Are ANY zombies still producing frames?
        bool anyZombieTimerActive = false;  // Are ANY zombie timers still firing?
        double maxZombieAgeMs = 0.0;

        std::string details;
    };

    ZombieAuditor() = default;

    // --- Process-level snapshots ---

    ProcessSnapshot captureProcessSnapshot() const {
        ProcessSnapshot snap;
        snap.timestamp = std::chrono::high_resolution_clock::now();
        snap.processId = GetCurrentProcessId();

        // Thread count
        snap.threadCount = countProcessThreads(snap.processId);

        // Handle counts
        HANDLE proc = GetCurrentProcess();
        GetProcessHandleCount(proc, &snap.handleCount);
        snap.gdiObjects = GetGuiResources(proc, GR_GDIOBJECTS);
        snap.userObjects = GetGuiResources(proc, GR_USEROBJECTS);

        // Memory
        PROCESS_MEMORY_COUNTERS_EX pmc = {};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(proc, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
            snap.workingSetBytes = pmc.WorkingSetSize;
            snap.privateBytes = pmc.PrivateUsage;
            snap.pagefileUsage = pmc.PagefileUsage;
        }

        // CPU
        FILETIME createTime, exitTime;
        GetProcessTimes(proc, &createTime, &exitTime, &snap.kernelTime, &snap.userTime);

        return snap;
    }

    ProcessDelta computeDelta(const ProcessSnapshot& before, const ProcessSnapshot& after) const {
        ProcessDelta d;
        d.elapsedMs = std::chrono::duration<double, std::milli>(
            after.timestamp - before.timestamp).count();

        d.threadDelta = after.threadCount - before.threadCount;
        d.handleDelta = static_cast<int>(after.handleCount) - static_cast<int>(before.handleCount);
        d.gdiDelta = static_cast<int>(after.gdiObjects) - static_cast<int>(before.gdiObjects);
        d.userObjectDelta = static_cast<int>(after.userObjects) - static_cast<int>(before.userObjects);

        d.workingSetDeltaKB = (static_cast<int64_t>(after.workingSetBytes) -
            static_cast<int64_t>(before.workingSetBytes)) / 1024;
        d.privateDeltaKB = (static_cast<int64_t>(after.privateBytes) -
            static_cast<int64_t>(before.privateBytes)) / 1024;

        d.cpuKernelDeltaMs = filetimeDeltaMs(before.kernelTime, after.kernelTime);
        d.cpuUserDeltaMs = filetimeDeltaMs(before.userTime, after.userTime);
        d.cpuTotalDeltaMs = d.cpuKernelDeltaMs + d.cpuUserDeltaMs;

        return d;
    }

    // --- Per-zombie behavioral audit ---

    std::vector<ZombieBehavior> auditZombies(const RendererManager& mgr) const {
        std::vector<ZombieBehavior> results;
        auto now = std::chrono::high_resolution_clock::now();

        for (const auto& [id, record] : mgr.records()) {
            if (!record.isZombie() &&
                record.attachment != RendererAttachment::Hidden) {
                continue;  // Only audit zombies and hidden engines
            }

            ZombieBehavior zb;
            zb.rendererId = id;
            zb.lastSurfaceId = record.surfaceId;
            zb.lifecycle = record.lifecycle;
            zb.attachment = record.attachment;

            // Timing
            zb.timeSinceCreationMs = std::chrono::duration<double, std::milli>(
                now - record.createdAt).count();

            if (record.isZombie()) {
                zb.ageMs = std::chrono::duration<double, std::milli>(
                    now - record.zombifiedAt).count();
            }

            // Frame activity
            zb.totalFramesAtZombify = 0;  // We don't have this yet, track in future
            zb.totalFramesNow = record.totalFramesRendered;
            zb.framesSinceZombify = 0;  // Needs baseline — computed during decay tracking

            // HWND state — get from renderer if accessible
            const auto* renderer = const_cast<RendererManager&>(mgr).getRenderer(id);
            if (renderer) {
                HWND childHwnd = renderer->childHwnd();
                if (childHwnd) {
                    zb.hwndState.rendererId = id;
                    zb.hwndState.hwnd = childHwnd;
                    zb.hwndState.isVisible = IsWindowVisible(childHwnd) != 0;
                    zb.hwndState.isEnabled = IsWindowEnabled(childHwnd) != 0;
                    zb.hwndState.hasParent = GetParent(childHwnd) != nullptr;
                    zb.hwndState.threadId = GetWindowThreadProcessId(childHwnd, nullptr);

                    // Check message queue
                    MSG msg;
                    zb.hwndState.pendingMessages = 0;
                    while (PeekMessage(&msg, childHwnd, 0, 0, PM_NOREMOVE)) {
                        zb.hwndState.pendingMessages++;
                        break;  // Just detect presence, don't consume
                    }

                    // Get thread CPU time for this HWND's thread
                    HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION,
                        FALSE, zb.hwndState.threadId);
                    if (hThread) {
                        FILETIME createTime, exitTime;
                        GetThreadTimes(hThread, &createTime, &exitTime,
                            &zb.threadKernelTime, &zb.threadUserTime);
                        CloseHandle(hThread);
                    }
                }
            }

            results.push_back(zb);
        }

        return results;
    }

    // --- Full audit run ---
    // Captures process baseline, audits all zombies, captures decay points.
    // decayIntervalMs: how long to wait between decay captures
    // decayCount: how many decay points to capture
    AuditResult runFullAudit(
        const RendererManager& mgr,
        int decayCount = 5,
        int decayIntervalMs = 1000) const
    {
        AuditResult result;
        result.scenario = "zombie_behavioral_audit";

        // Baseline process snapshot
        result.baselineSnapshot = captureProcessSnapshot();

        // Audit all current zombies
        result.zombieBehaviors = auditZombies(mgr);
        result.zombieCount = static_cast<int>(result.zombieBehaviors.size());

        // Capture decay curve
        for (int i = 0; i < decayCount; i++) {
            Sleep(static_cast<DWORD>(decayIntervalMs));

            // Pump messages during wait (keep system responsive)
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            DecayPoint dp;
            dp.processState = captureProcessSnapshot();
            dp.zombieStates = auditZombies(mgr);
            dp.elapsedSinceZombifyMs = std::chrono::duration<double, std::milli>(
                dp.processState.timestamp - result.baselineSnapshot.timestamp).count();

            // Aggregate zombie activity
            for (const auto& zs : dp.zombieStates) {
                dp.totalZombieFramesSinceZombify += zs.framesSinceZombify;
                dp.totalZombieTimers += zs.hwndState.timerCount;
                dp.totalZombiePendingMessages += zs.hwndState.pendingMessages;
            }

            result.decayCurve.push_back(dp);
        }

        // Post-audit snapshot
        result.postZombifySnapshot = captureProcessSnapshot();
        result.resourceDelta = computeDelta(result.baselineSnapshot, result.postZombifySnapshot);

        // Compute summary
        result.activeTimerCount = 0;
        result.totalFramesSinceZombify = 0;
        result.maxZombieAgeMs = 0.0;

        for (const auto& zb : result.zombieBehaviors) {
            if (zb.ageMs > result.maxZombieAgeMs) result.maxZombieAgeMs = zb.ageMs;
            result.totalFramesSinceZombify += zb.framesSinceZombify;
            result.activeTimerCount += zb.hwndState.timerCount;
        }

        result.anyZombieRendering = result.totalFramesSinceZombify > 0;
        result.anyZombieTimerActive = result.activeTimerCount > 0;

        // Build details string
        result.details = "Zombies=" + std::to_string(result.zombieCount) +
            " ThreadDelta=" + std::to_string(result.resourceDelta.threadDelta) +
            " HandleDelta=" + std::to_string(result.resourceDelta.handleDelta) +
            " GDIDelta=" + std::to_string(result.resourceDelta.gdiDelta) +
            " WorkingSetDeltaKB=" + std::to_string(result.resourceDelta.workingSetDeltaKB) +
            " PrivateDeltaKB=" + std::to_string(result.resourceDelta.privateDeltaKB) +
            " CPUDeltaMs=" + std::to_string(result.resourceDelta.cpuTotalDeltaMs);

        // Check CPU activity during observation window
        if (result.resourceDelta.cpuTotalDeltaMs > 100.0 && result.zombieCount > 0) {
            // More than 100ms of CPU during observation = zombies are active
            result.details += " [ACTIVE: significant CPU during observation]";
        }

        // Decay curve analysis
        if (result.decayCurve.size() >= 2) {
            const auto& first = result.decayCurve.front();
            const auto& last = result.decayCurve.back();

            auto firstCPU = filetimeToMs(first.processState.kernelTime) +
                            filetimeToMs(first.processState.userTime);
            auto lastCPU = filetimeToMs(last.processState.kernelTime) +
                           filetimeToMs(last.processState.userTime);
            double cpuDuringDecay = lastCPU - firstCPU;
            double decayWindowMs = last.elapsedSinceZombifyMs - first.elapsedSinceZombifyMs;

            if (decayWindowMs > 0) {
                double cpuPercent = (cpuDuringDecay / decayWindowMs) * 100.0;
                result.details += " DecayCPU=" + std::to_string(cpuPercent) + "%";

                // Check for persistent periodic activity
                if (cpuPercent > 5.0 && result.zombieCount > 0) {
                    result.details += " [WARNING: zombie CPU activity persists]";
                    result.passed = false;
                }
            }
        }

        // Per-zombie HWND visibility check
        for (const auto& zb : result.zombieBehaviors) {
            if (zb.hwndState.isVisible) {
                result.details += " [WARNING: zombie renderer #" +
                    std::to_string(zb.rendererId) + " HWND still visible]";
                result.passed = false;
            }
            if (zb.hwndState.pendingMessages > 0) {
                result.details += " [zombie #" + std::to_string(zb.rendererId) +
                    " has pending messages]";
            }
        }

        OutputDebugStringA(("ZOMBIE_AUDIT: " + result.details + "\n").c_str());

        return result;
    }

private:
    int countProcessThreads(DWORD processId) const {
        int count = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;

        THREADENTRY32 te;
        te.dwSize = sizeof(te);
        if (Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID == processId) count++;
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
        return count;
    }

    static double filetimeToMs(const FILETIME& ft) {
        ULARGE_INTEGER li;
        li.LowPart = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;
        return static_cast<double>(li.QuadPart) / 10000.0;  // 100ns units → ms
    }

    static double filetimeDeltaMs(const FILETIME& before, const FILETIME& after) {
        return filetimeToMs(after) - filetimeToMs(before);
    }
};

}  // namespace morphic
