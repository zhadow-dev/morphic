#pragma once

#include <string>

namespace morphic {

// Phase 3E.3 — Surface Role Architecture.
//
// Surface behavior derives from ROLE, not ad-hoc HWND flags.
// Role determines: topology policy, activation semantics, governance hints,
// compositor navigation participation, and window shell identity.
//
// This is the foundation for workspace orchestration.
// Roles are COMPOSITIONAL — each role maps to a fixed set of traits
// and a fixed topology policy. No boolean soup.
enum class SurfaceRole {
    Workspace,      // Default compositor-managed surface.
                    // Owned by main window, hidden from Alt+Tab/taskbar.
                    // Grouped activation, minimize-with-owner.

    Detached,       // User-promoted independent surface.
                    // Owned by main window (same destroy group) but
                    // WS_EX_APPWINDOW gives Alt+Tab/taskbar presence.
                    // Navigation independence, NOT process independence.

    ToolPalette,    // Floating utility window (inspector, controls).
                    // Owned, hidden from Alt+Tab/taskbar.
                    // Non-resizable (no WS_THICKFRAME).

    Overlay,        // HUD/display-only surface.
                    // Owned, compositor-local highest elevation tier.
                    // Never steals activation. Click-through (HTTRANSPARENT).
                    // NOT interactive — keyboard input goes elsewhere.
                    // For interactive floating surfaces, use ToolPalette.
};

// Activation semantics — more nuanced than "can activate yes/no".
// Each role maps to exactly one ActivationPolicy.
// This prevents the inevitable boolean explosion when roles multiply.
enum class ActivationPolicy {
    Normal,           // Standard Win32 activation on click.
                      // Used by: Workspace, ToolPalette

    NoActivate,       // WS_EX_NOACTIVATE — never becomes foreground.
                      // Click-through. No keyboard routing.
                      // Used by: Overlay

    IndependentFocus,  // Activatable, but participates in Alt+Tab
                       // independently of other surfaces.
                       // Used by: Detached

    // Future candidates (not implemented yet):
    // RedirectToMain,   // Activation redirects to main window
    // PreserveCurrent,  // Activation suppressed, current focus preserved
    // GroupedExclusive, // Only one in group active at a time
};

// Z-order policy — how a surface participates in z-order management.
// Roles imply z-order semantics. Without this, the runtime becomes inconsistent.
enum class ZOrderPolicy {
    Grouped,       // Raises with the app group when main/workspace activates.
                   // Normal compositor member. Workspace uses this.

    Independent,   // Only raises when DIRECTLY clicked/activated.
                   // Main window activation does NOT forcibly raise this.
                   // Detached uses this.

    Floating,      // Stays above Grouped surfaces within the app.
                   // Raised AFTER workspace surfaces so it floats above.
                   // ToolPalette uses this.

    Overlay,       // Compositor-local highest elevation tier.
                   // Realized via HWND_TOP insertion order — NOT WS_EX_TOPMOST.
                   // Above everything within the Morphic app, below other apps.
};

// Trait bundle — derived from role, never constructed manually.
// These drive topology policy derivation and governance hints.
struct SurfaceRoleTraits {
    bool showInAltTab;
    bool showInTaskbar;
    bool ownedByMainWindow;
    bool resizable;
    bool groupedActivation;    // minimize-with-owner, destroy-with-owner
    ActivationPolicy activation;
    ZOrderPolicy zOrder;
};

// Derive traits from role — single source of truth.
inline SurfaceRoleTraits traitsForRole(SurfaceRole role) {
    switch (role) {
        case SurfaceRole::Workspace:
            return {
                .showInAltTab = false,
                .showInTaskbar = false,
                .ownedByMainWindow = true,
                .resizable = true,
                .groupedActivation = true,
                .activation = ActivationPolicy::Normal,
                .zOrder = ZOrderPolicy::Grouped,
            };
        case SurfaceRole::Detached:
            return {
                .showInAltTab = true,
                .showInTaskbar = true,
                .ownedByMainWindow = false,  // Unowned to avoid Win32 owned/APPWINDOW conflict
                .resizable = true,
                .groupedActivation = false,  // Independent navigation
                .activation = ActivationPolicy::IndependentFocus,
                .zOrder = ZOrderPolicy::Independent,
            };
        case SurfaceRole::ToolPalette:
            return {
                .showInAltTab = false,
                .showInTaskbar = false,
                .ownedByMainWindow = true,
                .resizable = false,
                .groupedActivation = true,
                .activation = ActivationPolicy::Normal,
                .zOrder = ZOrderPolicy::Floating,
            };
        case SurfaceRole::Overlay:
            return {
                .showInAltTab = false,
                .showInTaskbar = false,
                .ownedByMainWindow = true,   // Part of app group (minimize/destroy with owner)
                .resizable = false,
                .groupedActivation = true,
                .activation = ActivationPolicy::NoActivate,
                .zOrder = ZOrderPolicy::Overlay,  // Compositor-local: highest within app
            };
    }
    // Unreachable — default to Workspace
    return traitsForRole(SurfaceRole::Workspace);
}

// String conversion — for method channel and debug output.
inline const char* toString(SurfaceRole role) {
    switch (role) {
        case SurfaceRole::Workspace:   return "workspace";
        case SurfaceRole::Detached:    return "detached";
        case SurfaceRole::ToolPalette: return "toolPalette";
        case SurfaceRole::Overlay:     return "overlay";
    }
    return "unknown";
}

inline const char* toString(ActivationPolicy p) {
    switch (p) {
        case ActivationPolicy::Normal:           return "normal";
        case ActivationPolicy::NoActivate:       return "noActivate";
        case ActivationPolicy::IndependentFocus: return "independentFocus";
    }
    return "unknown";
}

inline SurfaceRole roleFromString(const std::string& s) {
    if (s == "detached")    return SurfaceRole::Detached;
    if (s == "toolPalette") return SurfaceRole::ToolPalette;
    if (s == "overlay")     return SurfaceRole::Overlay;
    return SurfaceRole::Workspace;  // default
}

}  // namespace morphic
