#pragma once

#include "../core/types.h"
#include <windows.h>
#include <string>
#include <vector>
#include <sstream>

namespace morphic {

// Phase 1B Gate 5 — Crash Recovery Validation.
//
// Verifies that no orphan HWNDs remain after compositor shutdown.
// Does NOT actually kill the process — instead validates the cleanup path.
//
// Strategy:
// 1. Count MorphicSurface windows before test
// 2. Run compositor shutdown
// 3. Count MorphicSurface windows after shutdown
// 4. If count > 0, orphan windows remain — cleanup bug
class CrashRecoveryTest {
public:
    struct Result {
        bool passed = false;
        int orphansBefore = 0;
        int orphansAfter = 0;
        int hwndsBefore = 0;
        int hwndsAfter = 0;
        std::string details;
    };

    // Count all windows with MorphicSurface class name
    static int countMorphicWindows() {
        int count = 0;
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            wchar_t className[256];
            GetClassNameW(hwnd, className, 256);
            if (wcscmp(className, L"MorphicSurface") == 0) {
                (*reinterpret_cast<int*>(lParam))++;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&count));
        return count;
    }

    // Count windows that are children of a specific parent
    static int countChildWindows(HWND parent) {
        int count = 0;
        EnumChildWindows(parent, [](HWND hwnd, LPARAM lParam) -> BOOL {
            (*reinterpret_cast<int*>(lParam))++;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&count));
        return count;
    }

    // Validate that cleanup path works correctly.
    // Call AFTER compositor.shutdown() to verify 0 orphans remain.
    static Result validateCleanShutdown(int hwndCountBeforeShutdown) {
        Result r;
        r.hwndsBefore = hwndCountBeforeShutdown;
        r.hwndsAfter = countMorphicWindows();
        r.orphansAfter = r.hwndsAfter;
        r.passed = (r.orphansAfter == 0);

        if (r.passed) {
            r.details = "Clean shutdown: " + std::to_string(hwndCountBeforeShutdown) +
                " HWNDs created, 0 remain after shutdown";
        } else {
            r.details = "ORPHAN LEAK: " + std::to_string(r.orphansAfter) +
                " MorphicSurface HWNDs still exist after shutdown";
        }

        OutputDebugStringA(("CRASH RECOVERY: " + r.details + "\n").c_str());
        return r;
    }

    // Validate that GDI handles haven't leaked.
    // Should be called before and after test, delta should be near 0.
    static int getProcessHandleCount() {
        DWORD count = 0;
        GetProcessHandleCount(GetCurrentProcess(), &count);
        return static_cast<int>(count);
    }

    // Full lifecycle test: create surfaces, shut down, verify cleanup
    // Uses a callback to create test compositor and surfaces
    struct LifecycleResult {
        bool passed = false;
        int surfacesCreated = 0;
        int hwndsBeforeShutdown = 0;
        int hwndsAfterShutdown = 0;
        int handleDelta = 0;
        std::string details;
    };
};

}  // namespace morphic
