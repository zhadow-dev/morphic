#pragma once

#include "types.h"
#include "composition_surface.h"
#include "topology_manager.h"
#include "../rendering/renderer_surface.h"
#include <windows.h>
#include <windowsx.h>

namespace morphic {

class Compositor;

// Tier 3 — An actual HWND on the desktop.
// Transport layer. Owns the Win32 window, routes messages.
// In Phase 1A: 1:1 with CompositionSurface.
// Future: hosts multiple CompositionSurfaces.
//
// OWNERSHIP NOTE (Phase 2A):
// WindowHost does NOT own the renderer. RendererManager does.
// WindowHost holds a raw pointer (borrowed reference) to the
// currently active renderer. This is correct because renderer
// lifetime > surface lifetime (zombie engines outlive their surfaces).
class WindowHost {
public:
    WindowHost(CompositionSurface* surface, Compositor* compositor, HWND ownerWindow = nullptr);
    ~WindowHost();

    // --- Lifecycle ---
    bool create();
    void destroy();
    void show();
    void hide();

    // --- State ---
    HWND hwnd() const { return hwnd_; }
    CompositionSurface* surface() const { return surface_; }
    bool isAlive() const { return hwnd_ != nullptr; }

    // --- Position/Size (screen coordinates) ---
    void updatePosition(const Transform& t);
    RECT screenRect() const;

    // --- Window class registration (once per process) ---
    static bool RegisterWindowClass(HINSTANCE hInstance);
    static void UnregisterWindowClass(HINSTANCE hInstance);

    // --- Message handlers (called from WndProc) ---
    void onPaint();
    void onMouseDown(int screenX, int screenY);
    void onMouseMove(int screenX, int screenY);
    void onMouseUp(int screenX, int screenY);
    void onResize(int width, int height);
    LRESULT onHitTest(POINT screenPt);
    void onDpiChanged(UINT dpi, const RECT* suggested);

    // --- Renderer integration (Phase 2A) ---
    // WindowHost borrows a raw pointer from RendererManager.
    // The manager owns the renderer lifetime.
    void bindRenderer(RendererSurface* renderer, RenderId id);
    void unbindRenderer();
    RendererSurface* renderer() const { return renderer_; }
    RenderId activeRendererId() const { return activeRendererId_; }
    bool hasRenderer() const { return renderer_ != nullptr; }

    // --- Role topology (Phase 3E.3) ---
    SurfaceRole currentRole() const { return currentRole_; }

private:
    HWND hwnd_ = nullptr;
    HWND ownerWindow_ = nullptr;  // Main window — owner for activation group
    CompositionSurface* surface_ = nullptr;
    Compositor* compositor_ = nullptr;
    HBRUSH fillBrush_ = nullptr;
    SurfaceRole currentRole_ = SurfaceRole::Workspace;

    // --- Compositor-owned drag state ---
    // Full compositor drag replaces Win32's HTCAPTION modal loop.
    // Win32's modal loop (DefWindowProcW HTCAPTION) steals activation
    // authority and raises the surface above overlays. Compositor drag
    // keeps z-order frozen during drag via interaction transaction.
    bool dragging_ = false;
    int dragStartScreenX_ = 0;
    int dragStartScreenY_ = 0;
    int dragWindowStartX_ = 0;
    int dragWindowStartY_ = 0;

    // Borrowed reference — NOT owned. RendererManager owns the instance.
    RendererSurface* renderer_ = nullptr;
    RenderId activeRendererId_ = kInvalidRenderId;

    static constexpr int kDragHandleHeight = 32;
    static constexpr int kResizeBorder = 6;
    static constexpr int kResizeCoalesceMs = 16;  // Throttle renderer resize to ~60fps
    static constexpr wchar_t kClassName[] = L"MorphicSurface";
    static bool classRegistered_;
    DWORD lastRendererResizeMs_ = 0;  // Resize coalescing timestamp

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

}  // namespace morphic
