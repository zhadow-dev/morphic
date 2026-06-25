#pragma once

#include "../core/types.h"
#include "../display/display_manager.h"

#include <vector>
#include <string>

namespace morphic {

// Phase 1B Track 9 — Synthetic monitor simulation.
// Can't test real multi-monitor? Fake it.
// Simulates DPI changes, monitor bounds, hotplug events.
class VirtualDisplayTopology {
public:
    // --- Predefined topologies ---

    static std::vector<DisplayInfo> singleMonitor100() {
        DisplayInfo d;
        d.bounds = {0, 0, 1920, 1080};
        d.workArea = {0, 0, 1920, 1040};
        d.dpiX = 96.0f;
        d.dpiY = 96.0f;
        d.scaleFactor = 1.0f;
        d.refreshRate = 60;
        d.isPrimary = true;
        d.deviceName = L"\\\\.\\DISPLAY1";
        return {d};
    }

    static std::vector<DisplayInfo> dualMonitorMixed() {
        DisplayInfo d1;
        d1.bounds = {0, 0, 1920, 1080};
        d1.workArea = {0, 0, 1920, 1040};
        d1.dpiX = 96.0f;
        d1.dpiY = 96.0f;
        d1.scaleFactor = 1.0f;
        d1.refreshRate = 60;
        d1.isPrimary = true;
        d1.deviceName = L"\\\\.\\DISPLAY1";

        DisplayInfo d2;
        d2.bounds = {1920, 0, 3840, 1080};
        d2.workArea = {1920, 0, 3840, 1040};
        d2.dpiX = 144.0f;
        d2.dpiY = 144.0f;
        d2.scaleFactor = 1.5f;
        d2.refreshRate = 144;
        d2.isPrimary = false;
        d2.deviceName = L"\\\\.\\DISPLAY2";

        return {d1, d2};
    }

    static std::vector<DisplayInfo> tripleMonitor() {
        auto displays = dualMonitorMixed();

        DisplayInfo d3;
        d3.bounds = {-1920, 0, 0, 1080};
        d3.workArea = {-1920, 0, 0, 1040};
        d3.dpiX = 120.0f;
        d3.dpiY = 120.0f;
        d3.scaleFactor = 1.25f;
        d3.refreshRate = 120;
        d3.isPrimary = false;
        d3.deviceName = L"\\\\.\\DISPLAY3";

        displays.push_back(d3);
        return displays;
    }

    // --- DPI coordinate transform ---
    // Simulates what happens when a window moves from one DPI zone to another.

    static Transform transformForDpiChange(
        const Transform& t, float fromScale, float toScale)
    {
        if (fromScale <= 0 || toScale <= 0) return t;
        float ratio = toScale / fromScale;
        return {
            static_cast<int>(t.x * ratio),
            static_cast<int>(t.y * ratio),
            static_cast<int>(t.width * ratio),
            static_cast<int>(t.height * ratio)
        };
    }

    // --- Validation ---

    struct DpiTestResult {
        bool consistent = true;
        double maxError = 0.0;
        std::string details;
    };

    // Test: set position at DPI A, simulate DPI change to B, verify position adjusts correctly
    static DpiTestResult validateDpiConsistency(
        const Transform& original, float fromScale, float toScale)
    {
        DpiTestResult result;

        Transform adjusted = transformForDpiChange(original, fromScale, toScale);

        // Reverse transform should return to original (within rounding)
        Transform roundtrip = transformForDpiChange(adjusted, toScale, fromScale);

        double dx = std::abs(original.x - roundtrip.x);
        double dy = std::abs(original.y - roundtrip.y);
        double dw = std::abs(original.width - roundtrip.width);
        double dh = std::abs(original.height - roundtrip.height);

        result.maxError = (std::max)({dx, dy, dw, dh});
        result.consistent = result.maxError <= 1.0;  // Allow 1px rounding

        if (!result.consistent) {
            result.details = "DPI roundtrip error: " + std::to_string(result.maxError) +
                "px (from " + std::to_string(fromScale) + "x to " + std::to_string(toScale) + "x)";
        }
        return result;
    }

    // Validate a surface stays within monitor work area bounds
    static bool isWithinWorkArea(const Transform& t, const RECT& workArea) {
        return t.x >= workArea.left &&
               t.y >= workArea.top &&
               (t.x + t.width) <= workArea.right &&
               (t.y + t.height) <= workArea.bottom;
    }

    // Clamp a transform to fit within work area
    static Transform clampToWorkArea(const Transform& t, const RECT& workArea) {
        Transform clamped = t;
        int waWidth = workArea.right - workArea.left;
        int waHeight = workArea.bottom - workArea.top;

        if (clamped.width > waWidth) clamped.width = waWidth;
        if (clamped.height > waHeight) clamped.height = waHeight;

        if (clamped.x < workArea.left) clamped.x = workArea.left;
        if (clamped.y < workArea.top) clamped.y = workArea.top;
        if (clamped.x + clamped.width > workArea.right) clamped.x = workArea.right - clamped.width;
        if (clamped.y + clamped.height > workArea.bottom) clamped.y = workArea.bottom - clamped.height;

        return clamped;
    }
};

}  // namespace morphic
