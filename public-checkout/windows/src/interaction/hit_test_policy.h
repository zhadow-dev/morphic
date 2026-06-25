#pragma once

#include <windows.h>

namespace morphic {

// Phase 2A.2 — Extracted from WindowHost.
//
// HitTestPolicy determines what part of a surface HWND the cursor is over.
// This was previously embedded directly in WindowHost::onHitTest.
//
// Extracting this allows:
//   - Renderers to declare their own hit-test regions
//   - Role-aware hit testing (overlays, tool palettes)
//   - Future: per-surface hit-test customization
//   - Testing hit-test logic independently of HWND lifecycle
//
// THREAD: UI thread only (called from WndProc).
class HitTestPolicy {
public:
    struct HitTestConfig {
        int dragHandleHeight = 32;  // Top drag handle (px)
        int resizeBorder = 6;       // Edge resize border (px)
        bool hasRenderer = false;   // Whether a renderer is attached
        bool resizable = true;      // false for toolPalette, overlay
        bool nonInteractive = false; // true for overlay (click-through)
    };

    // Determine the NCHITTEST result for a point relative to the window.
    // clientX/clientY are relative to the window's top-left.
    // clientW/clientH are the window dimensions.
    static LRESULT hitTest(int clientX, int clientY,
                           int clientW, int clientH,
                           const HitTestConfig& config) {
        // Priority 1: Resize borders — narrow edge detection (6px).
        // Must be checked BEFORE drag handle (32px), otherwise the
        // top resize border is consumed by the drag handle.
        if (config.resizable) {
            bool onLeft   = clientX < config.resizeBorder;
            bool onRight  = clientX >= clientW - config.resizeBorder;
            bool onTop    = clientY < config.resizeBorder;
            bool onBottom = clientY >= clientH - config.resizeBorder;

            if (onTop && onLeft)     return HTTOPLEFT;
            if (onTop && onRight)    return HTTOPRIGHT;
            if (onBottom && onLeft)  return HTBOTTOMLEFT;
            if (onBottom && onRight) return HTBOTTOMRIGHT;
            if (onLeft)              return HTLEFT;
            if (onRight)             return HTRIGHT;
            if (onTop)               return HTTOP;
            if (onBottom)            return HTBOTTOM;
        }

        // Priority 2: Drag handle — works for ALL surfaces including overlays.
        // Returns HTCAPTION to enable Windows-native drag.
        if (clientY < config.dragHandleHeight) {
            return HTCAPTION;
        }

        // Priority 3: Non-interactive content (Overlay) — click-through.
        // Only the CONTENT area passes through — title bar was handled above.
        if (config.nonInteractive) return HTTRANSPARENT;

        // Priority 4: Normal content area.
        return HTCLIENT;
    }

    // Check if a point is in the drag handle region.
    // Used by WindowHost to decide whether to initiate drag on WM_LBUTTONDOWN.
    static bool isInDragHandle(int clientY, const HitTestConfig& config) {
        return clientY < config.dragHandleHeight;
    }

    // Check if a point is in the renderer content area (excludes drag handle
    // and resize borders).
    static bool isInRendererArea(int clientX, int clientY,
                                 int clientW, int clientH,
                                 const HitTestConfig& config) {
        if (!config.hasRenderer) return false;
        return clientX >= config.resizeBorder &&
               clientX < clientW - config.resizeBorder &&
               clientY >= config.dragHandleHeight &&
               clientY < clientH - config.resizeBorder;
    }
};

}  // namespace morphic
