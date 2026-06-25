#include "topology_manager.h"
#include "thread_affinity.h"
#include <string>

namespace morphic {

TopologyManager::TopologyPolicy TopologyManager::buildPolicy(SurfaceRole role, HWND mainWindow) {
    auto traits = traitsForRole(role);

    TopologyPolicy policy;
    policy.owner = traits.ownedByMainWindow ? mainWindow : nullptr;

    // Style: Detached surfaces use WS_OVERLAPPEDWINDOW for proper shell
    // registration (Alt+Tab, taskbar). Explorer heavily biases toward
    // overlapped windows for task switching. Custom chrome is preserved by
    // WM_NCCALCSIZE returning 0 and DWM NC rendering disabled.
    // All other surfaces use WS_POPUP (compositor-managed, no shell presence).
    if (role == SurfaceRole::Detached) {
        policy.style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    } else {
        policy.style = WS_POPUP | WS_CLIPCHILDREN;
        if (traits.resizable) {
            policy.style |= WS_THICKFRAME;
        }
    }

    // Extended style: derived from traits
    policy.exStyle = 0;

    if (traits.showInAltTab || traits.showInTaskbar) {
        // Detached: visible in Alt+Tab and taskbar
        policy.exStyle |= WS_EX_APPWINDOW;
    } else {
        // Workspace/ToolPalette/Overlay: hidden from Alt+Tab and taskbar
        policy.exStyle |= WS_EX_TOOLWINDOW;
    }

    // NOTE: WS_EX_TOPMOST is intentionally NEVER applied to any surface.
    // Elevation is compositor-local (HWND_TOP insertion order), not OS-global.
    // WS_EX_TOPMOST would contaminate the entire z-order band.

    if (traits.activation == ActivationPolicy::NoActivate) {
        policy.exStyle |= WS_EX_NOACTIVATE;
    }

    // Fallback: if no main window, force APPWINDOW (old behavior)
    if (!mainWindow) {
        policy.exStyle = WS_EX_APPWINDOW;
        policy.owner = nullptr;
    }

    return policy;
}

void TopologyManager::applyTopology(HWND hwnd, SurfaceRole role, HWND mainWindow) {
    MORPHIC_ASSERT_UI_THREAD();
    if (!hwnd) return;

    // Acquire TOPOLOGY authority before making any Win32 topology mutations
    MORPHIC_ACQUIRE_TOPOLOGY_AUTHORITY();

    auto policy = buildPolicy(role, mainWindow);

    // Update window styles
    SetWindowLongPtrW(hwnd, GWL_STYLE,
        static_cast<LONG_PTR>(policy.style | WS_VISIBLE));
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
        static_cast<LONG_PTR>(policy.exStyle));

    // Owner reassignment via GWLP_HWNDPARENT.
    // For WS_POPUP windows, this changes the OWNER (not parent).
    SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT,
        reinterpret_cast<LONG_PTR>(policy.owner));

    // SWP_FRAMECHANGED forces Win32 to re-read all style bits.
    // Without this, changes to WS_EX_TOOLWINDOW/WS_EX_APPWINDOW
    // may not take effect in the shell (Alt+Tab, taskbar).
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

}  // namespace morphic
