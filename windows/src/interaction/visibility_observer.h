#pragma once

#include "../rendering/visibility_state.h"
#include "../core/types.h"
#include <windows.h>
#include <unordered_map>
#include <chrono>

namespace morphic {

// Phase 2B — Visibility Observer
//
// Determines per-surface HWND visibility state using only public Win32 APIs.
// Compositor-owned. Feeds VisibilityState into the policy layer.
//
// Uses:
//   IsWindowVisible()       — basic visible/hidden
//   GetWindowPlacement()    — minimized detection
//   GetForegroundWindow()   — background detection
//   Z-order walk            — occlusion heuristic (future)
//
// Fires on: WM_ACTIVATE, WM_WINDOWPOSCHANGED, WM_SHOWWINDOW, periodic poll.
//
// IMPORTANT: Current implementation is HWND-centric.
// Future virtual surfaces or texture-only renderers may need
// abstract visibility semantics (noted, not urgent).
//
// THREAD: UI thread only.
class VisibilityObserver {
public:
    struct SurfaceVisibility {
        VisibilityState state = VisibilityState::Unknown;
        float confidence = 0.0f;  // 0.0 = unknown, 1.0 = certain
        std::chrono::high_resolution_clock::time_point lastChanged;
        std::chrono::high_resolution_clock::time_point lastObserved;
    };

    // Observe visibility for a surface HWND.
    // Call this when visibility-relevant events occur or on periodic poll.
    SurfaceVisibility observe(HWND surfaceHwnd, HWND mainAppHwnd) {
        if (!surfaceHwnd || !IsWindow(surfaceHwnd)) {
            return { VisibilityState::Detached, 1.0f };
        }

        auto now = std::chrono::high_resolution_clock::now();
        SurfaceVisibility result;
        result.lastObserved = now;

        // 1. Is the window visible at all?
        if (!IsWindowVisible(surfaceHwnd)) {
            result.state = VisibilityState::Hidden;
            result.confidence = 1.0f;
            return updateTracked(surfaceHwnd, result);
        }

        // 2. Is it minimized?
        WINDOWPLACEMENT wp = {};
        wp.length = sizeof(WINDOWPLACEMENT);
        if (GetWindowPlacement(surfaceHwnd, &wp)) {
            if (wp.showCmd == SW_SHOWMINIMIZED || wp.showCmd == SW_MINIMIZE) {
                result.state = VisibilityState::Minimized;
                result.confidence = 1.0f;
                return updateTracked(surfaceHwnd, result);
            }
        }

        // 3. Is the app in background? (not foreground)
        HWND fg = GetForegroundWindow();
        bool appIsForeground = false;
        if (fg) {
            // Check if foreground window belongs to our process
            DWORD fgPid = 0;
            GetWindowThreadProcessId(fg, &fgPid);
            DWORD ourPid = GetCurrentProcessId();
            appIsForeground = (fgPid == ourPid);
        }

        if (!appIsForeground) {
            result.state = VisibilityState::Background;
            result.confidence = 0.9f;  // might be partially visible
            return updateTracked(surfaceHwnd, result);
        }

        // 4. Occlusion check — is the surface fully covered by other windows?
        //    Using a simple heuristic: check if any point of the surface rect
        //    is the topmost point at that location via WindowFromPoint.
        //    This is imprecise (hence lower confidence for FullyOccluded).
        RECT surfaceRect;
        if (GetWindowRect(surfaceHwnd, &surfaceRect)) {
            // Sample center and 4 corner insets
            POINT samples[5] = {
                { (surfaceRect.left + surfaceRect.right) / 2,
                  (surfaceRect.top + surfaceRect.bottom) / 2 },
                { surfaceRect.left + 10, surfaceRect.top + 10 },
                { surfaceRect.right - 10, surfaceRect.top + 10 },
                { surfaceRect.left + 10, surfaceRect.bottom - 10 },
                { surfaceRect.right - 10, surfaceRect.bottom - 10 },
            };

            int visibleSamples = 0;
            for (const auto& pt : samples) {
                HWND atPoint = WindowFromPoint(pt);
                // WindowFromPoint returns the topmost window at that point.
                // If it's our surface or a child of our surface, it's visible there.
                if (atPoint == surfaceHwnd || IsChild(surfaceHwnd, atPoint)) {
                    visibleSamples++;
                }
            }

            if (visibleSamples == 0) {
                result.state = VisibilityState::FullyOccluded;
                result.confidence = 0.7f;  // heuristic, not certain
                return updateTracked(surfaceHwnd, result);
            }

            if (visibleSamples < 5) {
                result.state = VisibilityState::PartiallyVisible;
                result.confidence = 0.8f;
                return updateTracked(surfaceHwnd, result);
            }
        }

        // 5. Fully visible
        result.state = VisibilityState::Visible;
        result.confidence = 1.0f;
        return updateTracked(surfaceHwnd, result);
    }

    // Get the last observed state for a surface (without re-observing).
    const SurfaceVisibility* lastObserved(HWND surfaceHwnd) const {
        auto it = tracked_.find(surfaceHwnd);
        return (it != tracked_.end()) ? &it->second : nullptr;
    }

    // Remove tracking for a destroyed surface.
    void untrack(HWND surfaceHwnd) {
        tracked_.erase(surfaceHwnd);
    }

    // Clear all tracking.
    void clear() {
        tracked_.clear();
    }

private:
    SurfaceVisibility updateTracked(HWND hwnd, SurfaceVisibility result) {
        auto it = tracked_.find(hwnd);
        if (it != tracked_.end()) {
            // State changed?
            if (it->second.state != result.state) {
                result.lastChanged = result.lastObserved;
            } else {
                result.lastChanged = it->second.lastChanged;
            }
        } else {
            result.lastChanged = result.lastObserved;
        }
        tracked_[hwnd] = result;
        return result;
    }

    std::unordered_map<HWND, SurfaceVisibility> tracked_;
};

}  // namespace morphic
