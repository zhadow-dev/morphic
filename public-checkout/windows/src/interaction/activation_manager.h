#pragma once

#include "../core/types.h"
#include "../core/surface_role.h"
#include "../core/mutation_authority.h"
#include "../core/window_host.h"
#include <windows.h>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <memory>

namespace morphic {

class SurfaceRegistry;

// Phase 4 Step 1C — ActivationManager.
//
// AUTHORITY: Z-order realization.
// Translates semantic focus decisions into Win32 activation and z-order.
//
// KEY INVARIANT: The workspace stack (workspaceStack_) is the SINGLE
// canonical source of z-order truth for grouped surfaces. Realization
// iterates the stack, NOT the hosts map. This guarantees deterministic
// ordering regardless of unordered_map iteration order.
//
// THREAD: UI thread only (DeferWindowPos, SetForegroundWindow).
class ActivationManager {
public:
    ActivationManager() = default;

    void initialize(SurfaceRegistry* registry);

    void lock() { locked_ = true; }
    void unlock() { locked_ = false; }
    bool isLocked() const { return locked_; }

    // --- Surface lifecycle (canonical stack management) ---
    void registerSurface(NodeId surfaceId);
    void unregisterSurface(NodeId surfaceId);

    // --- Interaction Transaction Mode ---
    // Counter-based: supports overlapping drag + resize transactions.
    // Realization is suspended when transactionDepth_ > 0.
    // ONE authoritative reconciliation when depth returns to 0.
    void beginInteractionTransaction();
    void endInteractionTransaction();
    bool inInteractionTransaction() const { return transactionDepth_ > 0; }

    // --- Z-order realization ---

    // Phase 4 Step 3: Realizes a semantic focus decision by bringing the target
    // surface to the foreground.
    bool requestActivation(NodeId targetId);

    // Called when a surface's WndProc receives WM_ACTIVATE.
    // Builds z-order from canonical workspace stack (deterministic).
    void onSurfaceActivated(NodeId surfaceId);

    // Called when the main window (workspace shell) receives WM_ACTIVATE.
    void onMainWindowActivated();

    // --- Re-entrancy guard ---

    bool isHandlingActivation() const { return handlingActivation_; }
    bool isRealizingZOrder() const { return realizingZOrder_; }

private:
    // Three-tier DeferWindowPos: Grouped → Floating → Overlay.
    // ALL tiers use HWND_TOP. NONE use HWND_TOPMOST.
    void realizeZOrderTiers(
        const std::vector<HWND>& grouped,
        const std::vector<HWND>& floating,
        const std::vector<HWND>& overlay);

    // --- Canonical workspace stack ---
    // THE authoritative z-order for grouped surfaces.
    // Index 0 = bottom, last = top (most recently activated).
    // Maintained by: registerSurface, unregisterSurface, onSurfaceActivated.
    // Read by: realization (iterates stack, not hosts map).
    std::vector<NodeId> workspaceStack_;

    // Move a surface to the top of the workspace stack.
    void promoteInStack(NodeId surfaceId);

    SurfaceRegistry* registry_ = nullptr;
    bool handlingActivation_ = false;
    bool locked_ = true;
    int transactionDepth_ = 0;  // Counter, not bool — overlapping transactions
    bool realizingZOrder_ = false;
};

}  // namespace morphic
