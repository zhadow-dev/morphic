#pragma once

#include <windows.h>

namespace morphic {

// Phase 2A.2 — Extracted from WindowHost WndProc.
//
// MessageRouter classifies Win32 messages into categories for routing.
// This was previously inline logic in WindowHost::WndProc.
//
// Extracting this:
//   - Makes message routing testable independently
//   - Prevents renderer-specific routing from contaminating WindowHost
//   - Prepares for FocusManager (Step 8) which needs to intercept activation
//   - Prepares for CaptureManager (Step 7) which needs to intercept capture
//
// THREAD: UI thread only (called from WndProc).
class MessageRouter {
public:
    enum class Category {
        Layout,     // WM_SIZE, WM_PAINT, WM_NCHITTEST, etc. — Morphic handles
        Input,      // WM_MOUSE*, WM_KEY*, WM_POINTER* — forward to renderer
        Activation, // WM_ACTIVATE, WM_SETFOCUS, WM_KILLFOCUS — future: FocusManager
        System,     // WM_CLOSE, WM_NCDESTROY, WM_DPICHANGED — Morphic handles
        Unknown,    // Everything else — DefWindowProc
    };

    // Classify a Win32 message.
    static Category classify(UINT msg) {
        switch (msg) {
            // Layout — Morphic MUST handle these for spatial correctness
            case WM_SIZE:
            case WM_PAINT:
            case WM_NCHITTEST:
            case WM_NCCALCSIZE:
            case WM_NCPAINT:
            case WM_NCACTIVATE:
            case WM_ERASEBKGND:
            case WM_ENTERSIZEMOVE:
            case WM_EXITSIZEMOVE:
            case WM_GETMINMAXINFO:
            case WM_WINDOWPOSCHANGING:
            case WM_WINDOWPOSCHANGED:
                return Category::Layout;

            // Input — can be forwarded to renderer
            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_CHAR:
            case WM_UNICHAR:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_IME_STARTCOMPOSITION:
            case WM_IME_COMPOSITION:
            case WM_IME_ENDCOMPOSITION:
            case WM_IME_CHAR:
            case WM_POINTERDOWN:
            case WM_POINTERUP:
            case WM_POINTERUPDATE:
                return Category::Input;

            // Activation — future: routed through FocusManager
            case WM_ACTIVATE:
            case WM_SETFOCUS:
            case WM_KILLFOCUS:
            case WM_MOUSEACTIVATE:
                return Category::Activation;

            // System — Morphic handles lifecycle
            case WM_CLOSE:
            case WM_DESTROY:
            case WM_NCDESTROY:
            case WM_DPICHANGED:
            case WM_DISPLAYCHANGE:
                return Category::System;

            default:
                return Category::Unknown;
        }
    }

    // Should this message be forwarded to the renderer?
    // Only Input messages go to the renderer. Layout and System stay with Morphic.
    static bool shouldForwardToRenderer(UINT msg) {
        Category cat = classify(msg);
        return cat == Category::Input;
    }

    // Is this a layout message that Morphic must always handle?
    static bool isLayoutMessage(UINT msg) {
        return classify(msg) == Category::Layout;
    }

    // Is this an activation message? (Future: FocusManager intercepts these)
    static bool isActivationMessage(UINT msg) {
        return classify(msg) == Category::Activation;
    }
};

}  // namespace morphic
