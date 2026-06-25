#include "window_host.h"
#include "../composition/compositor.h"
#include "thread_affinity.h"
#include "../interaction/hit_test_policy.h"
#include "../interaction/message_router.h"
#include "runtime_commit_scheduler.h"
#include "runtime_mutation_intent.h"
#include "runtime_scene_state.h"
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace morphic {

bool WindowHost::classRegistered_ = false;

WindowHost::WindowHost(CompositionSurface* surface, Compositor* compositor, HWND ownerWindow)
    : surface_(surface), compositor_(compositor), ownerWindow_(ownerWindow) {
    // Create GDI brush from surface color
    uint32_t c = surface->color();
    fillBrush_ = CreateSolidBrush(RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
}

WindowHost::~WindowHost() {
    destroy();
    if (fillBrush_) {
        DeleteObject(fillBrush_);
        fillBrush_ = nullptr;
    }
}

bool WindowHost::RegisterWindowClass(HINSTANCE hInstance) {
    if (classRegistered_) return true;

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WindowHost::WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // We paint ourselves
    wc.lpszClassName = kClassName;

    if (RegisterClassExW(&wc) == 0) {
        return false;
    }
    classRegistered_ = true;
    return true;
}

void WindowHost::UnregisterWindowClass(HINSTANCE hInstance) {
    if (classRegistered_) {
        UnregisterClassW(kClassName, hInstance);
        classRegistered_ = false;
    }
}

bool WindowHost::create() {
    MORPHIC_ASSERT_UI_THREAD();
    if (hwnd_) return true;

    HINSTANCE hInstance = GetModuleHandle(nullptr);
    RegisterWindowClass(hInstance);

    const auto& t = surface_->worldTransform();

    // Phase 3E.3: Derive HWND topology from surface role.
    // ALL style decisions flow through TopologyManager — single source of truth.
    auto policy = TopologyManager::buildPolicy(surface_->role(), ownerWindow_);
    currentRole_ = surface_->role();

    OutputDebugStringA(("WINDOW_HOST: Creating surface #" +
        std::to_string(surface_->id()) + " role=" + toString(currentRole_) +
        " activation=" + toString(traitsForRole(currentRole_).activation) + "\n").c_str());

    hwnd_ = CreateWindowExW(
        policy.exStyle,
        kClassName,
        L"Morphic Surface",
        policy.style | WS_VISIBLE,
        t.x, t.y, t.width, t.height,
        policy.owner,  // For WS_POPUP: sets OWNER (not parent)
        nullptr,
        hInstance,
        this       // Pass WindowHost* via CREATESTRUCT
    );

    if (!hwnd_) return false;

    // --- Runtime topology verification ---
    HWND actualOwner = GetWindow(hwnd_, GW_OWNER);
    DWORD actualExStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
    OutputDebugStringA(("WINDOW_HOST: Surface #" +
        std::to_string(surface_->id()) +
        " role=" + toString(currentRole_) +
        " owner=" + (actualOwner ? "NON-NULL" : "null") +
        " APPWINDOW=" + ((actualExStyle & WS_EX_APPWINDOW) ? "yes" : "no") +
        " TOOLWINDOW=" + ((actualExStyle & WS_EX_TOOLWINDOW) ? "yes" : "no") +
        " TOPMOST=" + ((actualExStyle & WS_EX_TOPMOST) ? "yes" : "no") +
        " NOACTIVATE=" + ((actualExStyle & WS_EX_NOACTIVATE) ? "yes" : "no") +
        " CLIPCHILDREN=" + ((GetWindowLongPtrW(hwnd_, GWL_STYLE) & WS_CLIPCHILDREN) ? "yes" : "no") +
        "\n").c_str());

    // Kill DWM shadow and border for ALL surfaces (compositor chrome):
    MARGINS margins = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(hwnd_, &margins);

    DWMNCRENDERINGPOLICY dwmPolicy = DWMNCRP_DISABLED;
    DwmSetWindowAttribute(hwnd_, DWMWA_NCRENDERING_POLICY,
                          &dwmPolicy, sizeof(dwmPolicy));

    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    UpdateWindow(hwnd_);

    return true;
}

void WindowHost::destroy() {
    MORPHIC_ASSERT_UI_THREAD();
    // Unbind renderer — RendererManager handles lifecycle transitions.
    // WindowHost does NOT own the renderer.
    unbindRenderer();

    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void WindowHost::show() {
    if (!hwnd_) return;

    auto traits = traitsForRole(currentRole_);
    if (traits.zOrder == ZOrderPolicy::Independent) {
        // DESKTOP DOMAIN: Detached surfaces are real desktop participants.
        // SW_SHOW + SetForegroundWindow registers with shell as a
        // foreground-capable application window (Alt+Tab, taskbar).
        ShowWindow(hwnd_, SW_SHOW);
        SetForegroundWindow(hwnd_);
    } else {
        // COMPOSITOR DOMAIN: utility surfaces shown without activation.
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void WindowHost::hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void WindowHost::updatePosition(const Transform& t) {
    if (hwnd_) {
        SetWindowPos(hwnd_, nullptr, t.x, t.y, t.width, t.height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

RECT WindowHost::screenRect() const {
    RECT r{};
    if (hwnd_) GetWindowRect(hwnd_, &r);
    return r;
}

// --- Message handlers ---

void WindowHost::onPaint() {
    // If a Flutter renderer is attached, paint the drag handle (top 32px)
    // and resize border strips. Flutter renders in the remaining center.
    if (renderer_ && renderer_->isCreated() &&
        renderer_->type() != RendererSurface::Type::Gdi) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd_, &ps);

        RECT cr;
        GetClientRect(hwnd_, &cr);

        // Surface fill color for the frame
        uint32_t c = surface_->color();
        HBRUSH frameBrush = CreateSolidBrush(RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));

        // Top drag handle area (full width, 32px tall)
        RECT topHandle = { 0, 0, cr.right, kDragHandleHeight };
        FillRect(hdc, &topHandle, frameBrush);

        // Bottom resize strip
        RECT bot = { 0, cr.bottom - kResizeBorder, cr.right, cr.bottom };
        FillRect(hdc, &bot, frameBrush);
        // Left resize strip
        RECT left = { 0, kDragHandleHeight, kResizeBorder, cr.bottom - kResizeBorder };
        FillRect(hdc, &left, frameBrush);
        // Right resize strip
        RECT right = { cr.right - kResizeBorder, kDragHandleHeight, cr.right, cr.bottom - kResizeBorder };
        FillRect(hdc, &right, frameBrush);

        // Draw drag handle line + surface ID text
        HBRUSH lineBrush = CreateSolidBrush(RGB(255, 255, 255));
        RECT lineRect = { 0, kDragHandleHeight - 1, cr.right, kDragHandleHeight };
        FillRect(hdc, &lineRect, lineBrush);
        DeleteObject(lineBrush);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        wchar_t idText[64];
        swprintf_s(idText, L"Surface #%u [Flutter]", surface_->id());
        RECT handleTextRect = { 0, 0, cr.right, kDragHandleHeight };
        DrawTextW(hdc, idText, -1, &handleTextRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        DeleteObject(frameBrush);
        EndPaint(hwnd_, &ps);
        return;
    }

    // Fallback: GDI paint (Phase 1 surfaces)
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    FillRect(hdc, &ps.rcPaint, fillBrush_);

    // Draw a subtle drag handle indicator at the top
    RECT clientRect;
    GetClientRect(hwnd_, &clientRect);
    RECT handleRect = { 0, 0, clientRect.right, kDragHandleHeight };
    HBRUSH handleBrush = CreateSolidBrush(RGB(255, 255, 255));
    // Draw thin line at bottom of drag handle
    RECT lineRect = { 0, kDragHandleHeight - 1, clientRect.right, kDragHandleHeight };
    FillRect(hdc, &lineRect, handleBrush);
    DeleteObject(handleBrush);

    // Draw surface ID text
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    wchar_t idText[32];
    swprintf_s(idText, L"Surface #%u", surface_->id());
    DrawTextW(hdc, idText, -1, &handleRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    EndPaint(hwnd_, &ps);
}

void WindowHost::onMouseDown(int screenX, int screenY) {
    POINT screenPt;
    GetCursorPos(&screenPt);

    // Check if in drag handle zone
    POINT clientPt = screenPt;
    ScreenToClient(hwnd_, &clientPt);

    if (clientPt.y < kDragHandleHeight) {
        // Begin drag — screen-space coordinates
        SetCapture(hwnd_);
        dragging_ = true;
        if (compositor_) {
            compositor_->onDragBegin(surface_->id(), screenPt);
        }
    }
}

void WindowHost::onMouseMove(int screenX, int screenY) {
    if (dragging_) {
        POINT screenPt;
        GetCursorPos(&screenPt);
        if (compositor_) {
            compositor_->onDragUpdate(surface_->id(), screenPt);
        }
    }
}

void WindowHost::onMouseUp(int screenX, int screenY) {
    if (dragging_) {
        dragging_ = false;
        ReleaseCapture();
        POINT screenPt;
        GetCursorPos(&screenPt);
        if (compositor_) {
            compositor_->onDragEnd(surface_->id(), screenPt);
        }
    }
}

void WindowHost::onResize(int width, int height) {
    if (!compositor_ || !surface_ || !hwnd_) return;

    // CRITICAL: System-initiated resize (WM_SIZE) means the HWND is already
    // at its correct position and size. We need to sync the scene graph
    // to match reality — NOT request a frame that would call DeferWindowPos
    // and fight the system's ongoing resize operation.
    //
    // Left-edge resize changes BOTH X position and width simultaneously.
    RECT wr;
    GetWindowRect(hwnd_, &wr);
    int windowX = wr.left;
    int windowY = wr.top;
    int windowWidth = wr.right - wr.left;
    int windowHeight = wr.bottom - wr.top;

    // Update position AND size in scene graph
    surface_->setPosition(windowX, windowY);
    surface_->setSize(windowWidth, windowHeight);

    // Recompute world transform so debug overlay reads correct position
    surface_->computeWorldTransform();

    // Clear dirty — the HWND is already at this position/size.
    surface_->clearDirty();

    // --- Phase 9 execution kernel integration ---
    if (compositor_->commitScheduler()) {
        auto& state = compositor_->commitScheduler()->sceneState();
        state.updateRealizedGeometry(surface_->id(), Transform{windowX, windowY, windowWidth, windowHeight});
        state.updateRealizedVisibility(surface_->id(), IsWindowVisible(hwnd_) != FALSE);
        state.updateRealizedActivation(surface_->id(), GetActiveWindow() == hwnd_);

        // Enqueue high-priority GeometryChange intent to align desired semantic state and trigger post-commit renderer resize
        RuntimeMutationIntent intent;
        intent.type = RuntimeMutationIntent::Type::GeometryChange;
        intent.surfaceId = surface_->id();
        intent.priority = MutationPriority::Interactive;
        intent.geometry = Transform{windowX, windowY, windowWidth, windowHeight};
        compositor_->commitScheduler()->enqueueIntent(intent);
    } else {
        // Fallback for bootstrap / legacy modes: synchronously resize child renderer
        if (renderer_ && renderer_->isCreated()) {
            renderer_->resize(windowWidth, windowHeight);
        }
    }
}

LRESULT WindowHost::onHitTest(POINT screenPt) {
    RECT r;
    GetWindowRect(hwnd_, &r);

    int x = screenPt.x - r.left;
    int y = screenPt.y - r.top;
    int w = r.right - r.left;
    int h = r.bottom - r.top;

    // Role-aware hit test configuration.
    // Traits drive: resizability, interactivity, drag handle behavior.
    auto traits = traitsForRole(currentRole_);

    HitTestPolicy::HitTestConfig config;
    config.dragHandleHeight = kDragHandleHeight;
    config.resizeBorder = kResizeBorder;
    config.hasRenderer = hasRenderer();
    config.resizable = traits.resizable;
    config.nonInteractive = (currentRole_ == SurfaceRole::Overlay);

    return HitTestPolicy::hitTest(x, y, w, h, config);
}

void WindowHost::onDpiChanged(UINT dpi, const RECT* suggested) {
    if (suggested && hwnd_) {
        SetWindowPos(hwnd_, nullptr,
                     suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void WindowHost::bindRenderer(RendererSurface* renderer, RenderId id) {
    MORPHIC_ASSERT_UI_THREAD();
    if (renderer_) unbindRenderer();

    renderer_ = renderer;
    activeRendererId_ = id;

    if (hwnd_ && renderer_) {
        RECT r;
        GetClientRect(hwnd_, &r);
        renderer_->create(hwnd_, r.right - r.left, r.bottom - r.top);

        // Force synchronous repaint of the drag handle area.
        // The renderer-aware paint path draws "Surface #N [Flutter]".
        // Without this, the title only appears when WM_PAINT fires
        // naturally — which is delayed under multi-engine load.
        RECT handleRect = { 0, 0, r.right, kDragHandleHeight };
        InvalidateRect(hwnd_, &handleRect, FALSE);
        UpdateWindow(hwnd_);

        OutputDebugStringA(("WINDOW_HOST: Bound renderer #" +
            std::to_string(id) + " (" + renderer_->typeName() +
            ") to Surface #" + std::to_string(surface_->id()) + "\n").c_str());

        // Send synthetic WM_ACTIVATE(WA_ACTIVE) so the engine starts in
        // 'resumed' lifecycle state. Surfaces are created with SW_SHOWNOACTIVATE,
        // so they never receive a real WM_ACTIVATE from Windows. Without this,
        // Flutter engines start paused — animations show 'paused', rendering
        // is throttled, and the Dart side never receives onResume.
        LRESULT ignored;
        renderer_->handleMessage(hwnd_, WM_ACTIVATE, WA_ACTIVE, 0, &ignored);
    }
}

void WindowHost::unbindRenderer() {
    MORPHIC_ASSERT_UI_THREAD();
    if (renderer_) {
        OutputDebugStringA(("WINDOW_HOST: Unbinding renderer #" +
            std::to_string(activeRendererId_) + " from Surface #" +
            std::to_string(surface_->id()) + "\n").c_str());

        // Just clear the reference. RendererManager owns lifecycle.
        renderer_ = nullptr;
        activeRendererId_ = kInvalidRenderId;

        if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

// --- Static WndProc ---

LRESULT CALLBACK WindowHost::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WindowHost* host = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        host = reinterpret_cast<WindowHost*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
    } else {
        host = reinterpret_cast<WindowHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!host) return DefWindowProcW(hwnd, msg, wParam, lParam);

    // Phase 2A: Route messages to renderer, but NEVER let it consume
    // layout/paint messages — Morphic must always handle those for
    // spatial correctness (resize, hit-test, NC area, activation).
    //
    // COMPOSITOR DRAG BYPASS: During drag, mouse messages are owned by
    // the compositor. Do NOT forward to renderer — otherwise Flutter
    // consumes WM_MOUSEMOVE/WM_LBUTTONUP before the drag handler sees them.
    bool inCompositorDrag = host->dragging_;
    if (!inCompositorDrag && host->renderer_ && host->renderer_->isCreated()) {
        // Only forward messages Flutter needs for input/lifecycle.
        // Do NOT forward: WM_SIZE, WM_PAINT, WM_NCHITTEST, WM_NCCALCSIZE,
        // WM_NCPAINT, WM_NCACTIVATE, WM_ERASEBKGND, WM_ENTERSIZEMOVE,
        // WM_ACTIVATE, WM_CLOSE, WM_NCDESTROY, WM_DPICHANGED
        // Use MessageRouter to decide if this message goes to the renderer.
        // Layout, System, and Activation messages stay with Morphic.
        if (MessageRouter::shouldForwardToRenderer(msg)) {
            LRESULT result;
            if (host->renderer_->handleMessage(hwnd, msg, wParam, lParam, &result)) {
                return result;
            }
        }
    }

    switch (msg) {
        case WM_MOUSEACTIVATE: {
            // INTERACTION AUTHORITY: Morphic decides whether this click activates.
            // Without this handler, DefWindowProcW returns MA_ACTIVATE for ALL
            // clicks — the compositor has zero veto power over activation.
            auto traits = traitsForRole(host->surface_->role());
            if (traits.activation == ActivationPolicy::NoActivate) {
                // Overlay: NEVER activate on click. Click passes through.
                return MA_NOACTIVATE;
            }
            // All other roles: standard activation (DefWindowProcW default).
            break;
        }

        case WM_PAINT:
            host->onPaint();
            return 0;

        case WM_LBUTTONDOWN:
            host->onMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_MOUSEMOVE:
            // COMPOSITOR DRAG: move window directly (hot path).
            // Bypasses commit scheduler — direct SetWindowPos for smooth drag.
            if (host->dragging_) {
                POINT pt;
                GetCursorPos(&pt);
                int newX = host->dragWindowStartX_ + (pt.x - host->dragStartScreenX_);
                int newY = host->dragWindowStartY_ + (pt.y - host->dragStartScreenY_);
                SetWindowPos(hwnd, nullptr, newX, newY, 0, 0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                // Sync scene graph from OS (same pattern as WM_MOVE)
                host->surface_->setPosition(newX, newY);
                host->surface_->computeWorldTransform();
                host->surface_->clearDirty();
                if (host->compositor_ && host->compositor_->commitScheduler()) {
                    RECT wr;
                    GetWindowRect(hwnd, &wr);
                    host->compositor_->commitScheduler()->sceneState().updateRealizedGeometry(
                        host->surface_->id(),
                        Transform{newX, newY, wr.right - wr.left, wr.bottom - wr.top});
                }
                return 0;
            }
            if (host->compositor_) {
                host->compositor_->inputPhotonTracker().recordInputEvent();
            }
            host->onMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_LBUTTONUP:
            // COMPOSITOR DRAG: end drag + one reconciliation.
            if (host->dragging_) {
                host->dragging_ = false;
                ReleaseCapture();
                if (host->compositor_) {
                    host->compositor_->activationManager().endInteractionTransaction();
                    // Overlay: interaction without activation (Spec §10.2).
                    // Drag ends but NO activation/z-order realization.
                    auto dragEndTraits = traitsForRole(host->surface_->role());
                    if (dragEndTraits.activation != ActivationPolicy::NoActivate) {
                        host->compositor_->onSurfaceActivated(host->surface_->id());
                    }
                }
                return 0;
            }
            host->onMouseUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_NCLBUTTONDOWN:
            if (wParam == HTCAPTION) {
                auto traits = traitsForRole(host->surface_->role());

                // DETACHED (Independent): Use native DefWindowProcW drag.
                // Detached surfaces are desktop-participating application windows.
                // They MUST participate in OS foreground, snapping, task switching.
                // Native drag preserves these behaviors.
                if (traits.zOrder == ZOrderPolicy::Independent) {
                    break;  // Fall through to DefWindowProcW
                }

                // COMPOSITOR-MANAGED (Grouped): Compositor-owned drag.
                // DO NOT call DefWindowProcW — its modal loop steals activation
                // authority and raises the surface above overlays.
                POINT pt;
                GetCursorPos(&pt);
                host->dragStartScreenX_ = pt.x;
                host->dragStartScreenY_ = pt.y;
                RECT wr;
                GetWindowRect(hwnd, &wr);
                host->dragWindowStartX_ = wr.left;
                host->dragWindowStartY_ = wr.top;
                host->dragging_ = true;
                SetCapture(hwnd);
                if (host->compositor_) {
                    // Overlay: interaction without activation (Spec §10.2).
                    // Drag is allowed but NEVER triggers activation/z-order realization.
                    auto dragTraits = traitsForRole(host->surface_->role());
                    if (dragTraits.activation != ActivationPolicy::NoActivate) {
                        // SEMANTIC ACTIVATION FIRST: promote in workspace stack
                        // and realize z-order BEFORE freezing during drag.
                        host->compositor_->onSurfaceActivated(host->surface_->id());
                    }
                    // Freeze z-order during drag (all roles, including Overlay)
                    host->compositor_->activationManager().beginInteractionTransaction();
                }
                return 0;  // NOT DefWindowProcW — prevents modal loop
            }
            break;  // Other NC areas (resize borders): DefWindowProcW handles

        case WM_CAPTURECHANGED:
            // Safety net: if capture is stolen (e.g., by another app, system menu),
            // end drag cleanly to prevent stuck drag state.
            if (host->dragging_) {
                host->dragging_ = false;
                if (host->compositor_) {
                    host->compositor_->activationManager().endInteractionTransaction();
                    // Overlay: interaction without activation (Spec §10.2).
                    auto captureTraits = traitsForRole(host->surface_->role());
                    if (captureTraits.activation != ActivationPolicy::NoActivate) {
                        host->compositor_->onSurfaceActivated(host->surface_->id());
                    }
                }
            }
            return 0;

        case WM_SIZE:
            host->onResize(LOWORD(lParam), HIWORD(lParam));
            // Coalesce renderer resize: during the modal resize loop, Windows
            // sends dozens of WM_SIZE per frame. Each triggers Flutter's async
            // raster pipeline reset, causing jitter. Throttle to 16ms (~60fps)
            // so the renderer gets ONE resize per frame.
            if (host->renderer_ && host->renderer_->isCreated()) {
                DWORD now = GetTickCount();
                if (now - host->lastRendererResizeMs_ >= kResizeCoalesceMs) {
                    host->renderer_->resize(LOWORD(lParam), HIWORD(lParam));
                    host->lastRendererResizeMs_ = now;
                }
            }
            return 0;

        case WM_NCCALCSIZE:
            // Eliminate non-client area entirely.
            if (wParam == TRUE) return 0;
            break;

        case WM_NCPAINT:
            // Skip all non-client painting — kills the 1px system border
            return 0;

        // WM_WINDOWPOSCHANGING: NOT intercepted.
        // Previous attempts to block Windows z-order changes here also blocked:
        //   - owned-window raises during Alt+Tab (shell activation)
        //   - compositor's own DeferWindowPos/SetWindowPos
        // The correct fix is INTERACTION TRANSACTION MODE (see below).

        case WM_NCACTIVATE:
            // Prevent focus/unfocus border color change (white→grey).
            // Return TRUE to accept the activation but skip default NC redraw.
            return TRUE;

        case WM_NCHITTEST:
            return host->onHitTest({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });

        case WM_DPICHANGED:
            host->onDpiChanged(HIWORD(wParam), reinterpret_cast<RECT*>(lParam));
            return 0;

        case WM_ENTERSIZEMOVE:
            if (host->compositor_) {
                host->compositor_->activationManager().beginInteractionTransaction();
            }
            return 0;

        case WM_EXITSIZEMOVE:
            if (host->compositor_) {
                host->compositor_->activationManager().endInteractionTransaction();
                host->compositor_->onSurfaceActivated(host->surface_->id());
            }
            return 0;

        case WM_MOVE: {
            // Sync scene graph FROM OS during system-driven drag (HTCAPTION).
            // Windows owns the modal move loop. Compositor must follow, not fight.
            // Pattern mirrors onResize() — sync + clearDirty prevents compositor
            // from snapping the window back to the stale scene graph position.
            if (!host->compositor_ || !host->surface_ || !host->hwnd_) break;

            RECT wr;
            GetWindowRect(host->hwnd_, &wr);
            int wx = wr.left;
            int wy = wr.top;
            int ww = wr.right - wr.left;
            int wh = wr.bottom - wr.top;

            host->surface_->setPosition(wx, wy);
            host->surface_->computeWorldTransform();
            host->surface_->clearDirty();

            // Phase 9 execution kernel integration
            if (host->compositor_->commitScheduler()) {
                auto& state = host->compositor_->commitScheduler()->sceneState();
                state.updateRealizedGeometry(host->surface_->id(),
                    Transform{wx, wy, ww, wh});
            }
            return 0;
        }

        case WM_ACTIVATE:
            // Step 1: Morphic handles z-order management
            if (LOWORD(wParam) != WA_INACTIVE && host->compositor_) {
                host->compositor_->onSurfaceActivated(host->surface_->id());
            }
            // Step 2: Only forward WA_ACTIVE to Flutter — NEVER WA_INACTIVE.
            // In a multi-surface compositor, losing activation to another Morphic
            // surface is NOT the same as the app going to background. Forwarding
            // WA_INACTIVE causes Flutter to enter paused lifecycle and stop
            // rendering — which freezes all non-focused surfaces.
            // All engines must keep rendering at all times.
            // Text cursor management is handled by the Flutter child HWND:
            // when we SetFocus(child2), child1 gets WM_KILLFOCUS from Windows,
            // which Flutter's own child WndProc processes to clear the cursor.
            if (LOWORD(wParam) != WA_INACTIVE && host->renderer_ && host->renderer_->isCreated()) {
                LRESULT ignored;
                host->renderer_->handleMessage(hwnd, msg, wParam, lParam, &ignored);
            }
            // Step 3: When activated, transfer keyboard focus to Flutter child HWND.
            if (LOWORD(wParam) != WA_INACTIVE) {
                HWND child = (host->renderer_ && host->renderer_->isCreated())
                    ? host->renderer_->childHwnd() : nullptr;
                if (child) {
                    SetFocus(child);
                }
            }
            return 0;

        case WM_SETFOCUS: {
            // Parent got focus directly (e.g., from another app via Alt+Tab).
            // Transfer to Flutter child immediately. Do NOT forward WM_SETFOCUS
            // to Flutter — it causes a focus fight where parent sends SETFOCUS,
            // Flutter calls SetFocus(child), parent gets KILLFOCUS, Flutter
            // thinks it lost focus. Instead, just move focus to the child.
            HWND child = (host->renderer_ && host->renderer_->isCreated())
                ? host->renderer_->childHwnd() : nullptr;
            if (child) {
                SetFocus(child);
            }
            return 0;
        }

        case WM_KILLFOCUS: {
            // Only forward to Flutter if focus is LEAVING our app entirely.
            // If focus is moving to our own Flutter child (expected after
            // SetFocus(child)), do NOT forward — Flutter already has focus
            // via the child's WndProc receiving WM_SETFOCUS directly.
            HWND child = (host->renderer_ && host->renderer_->isCreated())
                ? host->renderer_->childHwnd() : nullptr;
            HWND gainingFocus = reinterpret_cast<HWND>(wParam);
            if (gainingFocus != child && host->renderer_ && host->renderer_->isCreated()) {
                LRESULT ignored;
                host->renderer_->handleMessage(hwnd, msg, wParam, lParam, &ignored);
            }
            return 0;
        }

        case WM_CLOSE:
            // Don't allow individual surface close — compositor manages lifecycle
            return 0;

        case WM_NCDESTROY:
            // Final message — clear user data to prevent dangling access
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;

        case WM_ERASEBKGND: {
            // Paint fillBrush_ as background. This fires synchronously
            // during CreateWindowEx (via internal ShowWindow), ensuring
            // the surface never appears with undefined visual state.
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc;
            GetClientRect(hwnd, &rc);

            if (host->renderer_ && host->renderer_->isCreated()) {
                // Renderer attached: only erase non-renderer regions.
                // WS_CLIPCHILDREN handles most exclusion, but explicit
                // region painting prevents edge artifacts during resize.
                // Top drag handle
                RECT top = { 0, 0, rc.right, host->kDragHandleHeight };
                FillRect(hdc, &top, host->fillBrush_);
                // Bottom resize border
                RECT bot = { 0, rc.bottom - host->kResizeBorder, rc.right, rc.bottom };
                FillRect(hdc, &bot, host->fillBrush_);
                // Left resize border
                RECT lft = { 0, host->kDragHandleHeight, host->kResizeBorder,
                             rc.bottom - host->kResizeBorder };
                FillRect(hdc, &lft, host->fillBrush_);
                // Right resize border
                RECT rgt = { rc.right - host->kResizeBorder, host->kDragHandleHeight,
                             rc.right, rc.bottom - host->kResizeBorder };
                FillRect(hdc, &rgt, host->fillBrush_);
            } else {
                // No renderer: fill entire background
                FillRect(hdc, &rc, host->fillBrush_);
            }
            return 1;  // We handled the erase
        }

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace morphic
