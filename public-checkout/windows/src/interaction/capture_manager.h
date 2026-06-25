#pragma once

#include "../core/types.h"
#include "../core/thread_affinity.h"
#include <windows.h>
#include <string>

namespace morphic {

// Phase 2A.2 — Capture ownership management.
//
// Tracks which surface/renderer owns pointer capture, drag, resize,
// and gesture ownership. Without this, multiple HWNDs + multiple
// renderers create competing interaction domains:
//   - Lost drags (capture stolen by another surface)
//   - Stuck resize (capture not released)
//   - Phantom activation (wrong surface receives input)
//   - Broken gestures (interrupted by focus change)
//
// RULES:
//   1. Only ONE surface can own pointer capture at a time
//   2. Only ONE surface can be in drag mode at a time
//   3. Only ONE surface can be in resize mode at a time
//   4. Capture transfers must be explicit (no implicit stealing)
//   5. Capture loss (WM_CAPTURECHANGED) must be handled gracefully
//
// THREAD: UI thread only.
class CaptureManager {
public:
    enum class CaptureType {
        None,
        PointerCapture,  // SetCapture() active
        Drag,            // Surface being dragged
        Resize,          // Surface being resized
        Gesture,         // Future: multi-touch gesture
    };

    struct CaptureState {
        CaptureType type = CaptureType::None;
        NodeId surfaceId = 0;       // Surface that owns capture
        RenderId rendererId = 0;    // Renderer that initiated (if any)
        HWND captureHwnd = nullptr; // HWND with SetCapture
        bool isActive = false;
    };

    // Metrics for observability.
    struct CaptureMetrics {
        int totalCaptures = 0;      // Total capture acquisitions
        int totalReleases = 0;      // Total capture releases
        int captureStolen = 0;      // Times capture was lost unexpectedly
        int dragCount = 0;          // Total drags initiated
        int resizeCount = 0;        // Total resizes initiated
    };

    CaptureManager() = default;

    // --- Capture acquisition ---

    // Acquire pointer capture for a surface.
    // Returns false if another surface already holds capture.
    bool acquireCapture(NodeId surfaceId, CaptureType type,
                        HWND hwnd, RenderId rendererId = 0) {
        MORPHIC_ASSERT_UI_THREAD();

        if (state_.isActive && state_.surfaceId != surfaceId) {
            // Another surface holds capture — deny
            OutputDebugStringA(("CAPTURE_MGR: DENIED — Surface #" +
                std::to_string(surfaceId) + " requested " + typeName(type) +
                " but Surface #" + std::to_string(state_.surfaceId) +
                " holds " + typeName(state_.type) + "\n").c_str());
            return false;
        }

        state_.type = type;
        state_.surfaceId = surfaceId;
        state_.rendererId = rendererId;
        state_.captureHwnd = hwnd;
        state_.isActive = true;

        metrics_.totalCaptures++;
        if (type == CaptureType::Drag) metrics_.dragCount++;
        if (type == CaptureType::Resize) metrics_.resizeCount++;

        return true;
    }

    // Release capture for a surface.
    // Only the owning surface can release.
    void releaseCapture(NodeId surfaceId) {
        MORPHIC_ASSERT_UI_THREAD();

        if (!state_.isActive) return;
        if (state_.surfaceId != surfaceId) {
            OutputDebugStringA(("CAPTURE_MGR: WARNING — Surface #" +
                std::to_string(surfaceId) + " tried to release capture owned by Surface #" +
                std::to_string(state_.surfaceId) + "\n").c_str());
            return;
        }

        state_ = CaptureState{};
        metrics_.totalReleases++;
    }

    // Called when WM_CAPTURECHANGED fires — capture was stolen by the system
    // or another window. Must handle gracefully.
    void onCaptureLost(HWND lostTo) {
        MORPHIC_ASSERT_UI_THREAD();

        if (state_.isActive) {
            OutputDebugStringA(("CAPTURE_MGR: Capture STOLEN from Surface #" +
                std::to_string(state_.surfaceId) + " (" + typeName(state_.type) +
                ")\n").c_str());
            metrics_.captureStolen++;
            state_ = CaptureState{};
        }
    }

    // Force-release all capture (used during shutdown/error recovery).
    void forceReleaseAll() {
        MORPHIC_ASSERT_UI_THREAD();
        if (state_.isActive) {
            if (state_.captureHwnd) {
                ::ReleaseCapture();
            }
            state_ = CaptureState{};
        }
    }

    // --- Queries ---

    bool isActive() const { return state_.isActive; }
    CaptureType activeType() const { return state_.type; }
    NodeId activeSurface() const { return state_.surfaceId; }
    const CaptureState& state() const { return state_; }
    const CaptureMetrics& metrics() const { return metrics_; }

    bool isDragging() const { return state_.isActive && state_.type == CaptureType::Drag; }
    bool isResizing() const { return state_.isActive && state_.type == CaptureType::Resize; }
    bool isDragging(NodeId id) const { return isDragging() && state_.surfaceId == id; }

private:
    static const char* typeName(CaptureType t) {
        switch (t) {
            case CaptureType::None: return "None";
            case CaptureType::PointerCapture: return "PointerCapture";
            case CaptureType::Drag: return "Drag";
            case CaptureType::Resize: return "Resize";
            case CaptureType::Gesture: return "Gesture";
            default: return "Unknown";
        }
    }

    CaptureState state_;
    CaptureMetrics metrics_;
};

}  // namespace morphic
