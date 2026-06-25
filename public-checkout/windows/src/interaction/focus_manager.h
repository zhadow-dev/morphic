#pragma once

#include "../core/types.h"
#include "../core/thread_affinity.h"
#include <windows.h>
#include <string>
#include <vector>
#include <chrono>

namespace morphic {

// Phase 2A.2 — Focus and activation management.
//
// In a multi-HWND compositor, focus is DANGEROUS:
//   - WM_ACTIVATE fires on every window focus change
//   - SetForegroundWindow can steal focus from the user's app
//   - Renderer activation (Flutter) must not fight compositor activation
//   - IME context is per-HWND — wrong focus = wrong IME target
//   - Z-order changes during activation can corrupt elevation
//
// FocusManager is the SINGLE AUTHORITY for which surface is "active."
// WindowHost must NEVER call SetForegroundWindow directly.
// All focus transitions go through FocusManager.
//
// INVARIANTS:
//   1. At most ONE surface is active at a time
//   2. Focus transitions are logged for debugging
//   3. Activation during drag is suppressed (prevents z-order thrash)
//   4. Renderer activation follows surface activation (never leads)
//   5. Tab traversal order matches elevation order (future)
//
// THREAD: UI thread only.
class FocusManager {
public:
    struct FocusState {
        NodeId activeSurfaceId = 0;       // Currently active surface (0 = none)
        RenderId activeRendererId = 0;    // Renderer in the active surface
        HWND activeHwnd = nullptr;        // HWND with keyboard focus
        bool suppressActivation = false;  // True during drag (prevents z-thrash)
    };

    struct FocusTransition {
        NodeId fromSurface = 0;
        NodeId toSurface = 0;
        std::string reason;
        std::chrono::high_resolution_clock::time_point timestamp;
    };

    struct FocusMetrics {
        int totalTransitions = 0;         // Total focus changes
        int suppressedTransitions = 0;    // Blocked by suppressActivation
        int rendererActivations = 0;      // Times a renderer was activated
        int focusStolen = 0;              // External focus steal
    };

    static constexpr size_t kMaxHistory = 100;

    FocusManager() = default;

    // --- Focus transitions ---

    // Request activation of a surface.
    // Returns false if activation was suppressed (e.g., during drag).
    bool requestActivation(NodeId surfaceId, HWND hwnd,
                           RenderId rendererId = 0,
                           const std::string& reason = "") {
        MORPHIC_ASSERT_UI_THREAD();

        if (state_.suppressActivation) {
            metrics_.suppressedTransitions++;
            OutputDebugStringA(("FOCUS_MGR: SUPPRESSED activation of Surface #" +
                std::to_string(surfaceId) + " (reason: " + reason + ")\n").c_str());
            return false;
        }

        if (state_.activeSurfaceId == surfaceId) {
            return true;  // Already active
        }

        // Log transition
        FocusTransition transition;
        transition.fromSurface = state_.activeSurfaceId;
        transition.toSurface = surfaceId;
        transition.reason = reason;
        transition.timestamp = std::chrono::high_resolution_clock::now();
        history_.push_back(transition);
        if (history_.size() > kMaxHistory) {
            history_.erase(history_.begin());
        }

        NodeId previousId = state_.activeSurfaceId;
        state_.activeSurfaceId = surfaceId;
        state_.activeRendererId = rendererId;
        state_.activeHwnd = hwnd;

        metrics_.totalTransitions++;
        if (rendererId != 0) metrics_.rendererActivations++;

        OutputDebugStringA(("FOCUS_MGR: Surface #" +
            std::to_string(previousId) + " -> Surface #" +
            std::to_string(surfaceId) + " (" + reason + ")\n").c_str());

        return true;
    }

    // Deactivate a surface (e.g., on WM_KILLFOCUS, surface hidden).
    void deactivate(NodeId surfaceId) {
        MORPHIC_ASSERT_UI_THREAD();

        if (state_.activeSurfaceId == surfaceId) {
            FocusTransition transition;
            transition.fromSurface = surfaceId;
            transition.toSurface = 0;
            transition.reason = "deactivate";
            transition.timestamp = std::chrono::high_resolution_clock::now();
            history_.push_back(transition);
            if (history_.size() > kMaxHistory) {
                history_.erase(history_.begin());
            }

            state_.activeSurfaceId = 0;
            state_.activeRendererId = 0;
            state_.activeHwnd = nullptr;
            metrics_.totalTransitions++;
        }
    }

    // Called when focus leaves all Morphic windows (external steal).
    void onExternalFocusSteal() {
        MORPHIC_ASSERT_UI_THREAD();

        if (state_.activeSurfaceId != 0) {
            OutputDebugStringA(("FOCUS_MGR: External focus steal from Surface #" +
                std::to_string(state_.activeSurfaceId) + "\n").c_str());
            metrics_.focusStolen++;
            deactivate(state_.activeSurfaceId);
        }
    }

    // --- Suppression ---

    // Suppress activation during drag to prevent z-order thrashing.
    void suppressDuringDrag() {
        MORPHIC_ASSERT_UI_THREAD();
        state_.suppressActivation = true;
    }

    void resumeAfterDrag() {
        MORPHIC_ASSERT_UI_THREAD();
        state_.suppressActivation = false;
    }

    bool isSuppressed() const { return state_.suppressActivation; }

    // --- Queries ---

    NodeId activeSurface() const { return state_.activeSurfaceId; }
    RenderId activeRenderer() const { return state_.activeRendererId; }
    HWND activeHwnd() const { return state_.activeHwnd; }
    bool isActive(NodeId surfaceId) const { return state_.activeSurfaceId == surfaceId; }
    const FocusState& state() const { return state_; }
    const FocusMetrics& metrics() const { return metrics_; }
    const std::vector<FocusTransition>& history() const { return history_; }

private:
    FocusState state_;
    FocusMetrics metrics_;
    std::vector<FocusTransition> history_;
};

}  // namespace morphic
